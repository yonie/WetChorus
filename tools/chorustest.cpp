//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//
// chorustest - measures ChorusEngine directly, with no plugin and no host.
//
// Compiles against the same source the plugin uses, so a constant that does
// not do what its name says shows up here in a second rather than after a full
// VST3 build and a hand test in a DAW.
//
// What it measures:
//   * the resting delay, which has to land in the single-digit milliseconds a
//     chorus lives at rather than the tens a delay does;
//   * the delay sweeping with DEPTH, and the sweep RATE following SPEED;
//   * the delay line's bandwidth tracking the clock - the one property that
//     separates a modelled bucket brigade from a swept fractional delay;
//   * what each output actually carries at the two ends of MODE, which is the
//     whole stereo claim: at CHORUS one output must be the dry signal, at VIB
//     neither may be;
//   * the crossfade being linear through the middle of MODE;
//   * the noise floor, which is meant to be audible rather than scrubbed.
//
// Build (from the plugin repo root):
//   cl /EHsc /O2 /std:c++17 /I WetChorus/source tools/chorustest.cpp ^
//      WetChorus/source/chorusengine.cpp
//------------------------------------------------------------------------

#include "chorusengine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

using namespace Yonie;

namespace {

constexpr double kPI = 3.14159265358979323846;
constexpr double kRate = 44100.0;

int failures = 0;

double dB(double lin) { return 20.0 * std::log10(lin > 1e-12 ? lin : 1e-12); }

void check(bool ok, const char* what, const char* detail = "")
{
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
                detail[0] ? "  " : "", detail);
    if (!ok) ++failures;
}

//------------------------------------------------------------------------
// Run the engine over a signal and hand back both outputs.
void run(ChorusEngine& e, const std::vector<float>& in,
         std::vector<float>& outL, std::vector<float>& outR)
{
    const int n = static_cast<int>(in.size());
    outL.assign(static_cast<size_t>(n), 0.0f);
    outR.assign(static_cast<size_t>(n), 0.0f);
    // Block by block, as a host would, so the parameter glide is exercised.
    const int block = 512;
    for (int i = 0; i < n; i += block)
    {
        const int m = std::min(block, n - i);
        e.processStereo(in.data() + i, in.data() + i,
                        outL.data() + i, outR.data() + i, m);
    }
}

std::vector<float> sine(double freq, int n, double amp = 0.25)
{
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t>(i)] =
            static_cast<float>(amp * std::sin(2.0 * kPI * freq * i / kRate));
    return x;
}

// The probe for anything that measures DELAY.
//
// A sine is useless for this: its autocorrelation repeats every period, so a
// 256-sample delay measured against a 700 Hz tone comes back as 9 samples,
// 67, 130 or 193 depending on which alias happens to win. That cost a round
// of chasing a DSP bug that was not there. Noise has one sharp peak and no
// aliases.
std::vector<float> probeNoise(int n, double amp = 0.25)
{
    std::vector<float> x(static_cast<size_t>(n));
    uint32_t st = 0x13579BDFu;
    for (int i = 0; i < n; ++i)
    {
        st ^= st << 13; st ^= st >> 17; st ^= st << 5;
        x[static_cast<size_t>(i)] =
            static_cast<float>(amp * (int32_t(st) * 4.6566129e-10));
    }
    return x;
}

double rms(const std::vector<float>& x, int from, int to)
{
    double s = 0.0;
    for (int i = from; i < to; ++i) s += double(x[size_t(i)]) * x[size_t(i)];
    return std::sqrt(s / std::max(1, to - from));
}

// Best matching lag, in samples, between a reference and a signal.
int bestLag(const std::vector<float>& ref, const std::vector<float>& sig,
            int from, int len, int maxLag)
{
    int best = 0;
    double bestScore = -1e30;
    for (int lag = 1; lag < maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i < len; ++i)
            s += double(ref[size_t(from + i - lag)]) * sig[size_t(from + i)];
        if (s > bestScore) { bestScore = s; best = lag; }
    }
    return best;
}

// As bestLag, but over a known range. The resting delay is about 262 samples
// and DEPTH moves it by at most 100 either way, so searching 1..700 for every
// window of a thirty-second capture is mostly wasted work.
int bestLagIn(const std::vector<float>& ref, const std::vector<float>& sig,
              int from, int len, int loLag, int hiLag)
{
    int best = loLag;
    double bestScore = -1e30;
    for (int lag = loLag; lag <= hiLag; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i < len; ++i)
            s += double(ref[size_t(from + i - lag)]) * sig[size_t(from + i)];
        if (s > bestScore) { bestScore = s; best = lag; }
    }
    return best;
}

// The strongest frequency in a slowly varying sequence, by scanning a DFT bin
// across the range of interest.
//
// This replaced a mean-crossing count, which is not robust enough for the job:
// the correlation window is 23 ms wide and the delay moves inside it, so the
// lag estimate jitters, and every jitter across the mean reads as another
// reversal. It reported a 3.7 Hz sweep as 8.6 Hz. Adding hysteresis then swung
// it the other way and under-counted. A DFT does not care about either.
double dominantHz(const std::vector<int>& seq, double seqRate,
                  double loHz, double hiHz)
{
    double mean = 0.0;
    for (int v : seq) mean += v;
    mean /= double(seq.size());

    double bestHz = loHz, bestMag = -1.0;
    const int steps = 900;
    for (int k = 0; k <= steps; ++k)
    {
        // Geometric scan, because the parameter itself is exponential.
        const double f = loHz * std::pow(hiHz / loHz, double(k) / steps);
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < seq.size(); ++i)
        {
            const double ph = 2.0 * kPI * f * double(i) / seqRate;
            const double v = seq[i] - mean;
            re += v * std::cos(ph);
            im += v * std::sin(ph);
        }
        const double mag = std::hypot(re, im);
        if (mag > bestMag) { bestMag = mag; bestHz = f; }
    }
    return bestHz;
}

ChorusEngine::Settings make(int speed, int depth, int mode)
{
    ChorusEngine::Settings s;
    s.speed = speed; s.depth = depth; s.mode = mode;
    return s;
}

//------------------------------------------------------------------------
void testRestingDelay()
{
    std::printf("\nResting delay\n");
    ChorusEngine e;
    e.prepare(kRate, 512);
    e.setNoiseEnabled(false);
    e.setSettings(make(ChorusRange::kDefaultSpeed, ChorusRange::kShallow,
                       ChorusRange::kSteps - 1));

    const int n = 8192;
    auto in = probeNoise(n);
    std::vector<float> l, r;
    run(e, in, l, r);

    const int lag = bestLag(in, l, 4096, 2048, 600);
    const double ms = lag * 1000.0 / kRate;
    char buf[128];
    std::snprintf(buf, sizeof buf, "measured %.2f ms (%d samples)", ms, lag);
    // A chorus lives at the short end of the device's range. Tens of
    // milliseconds would be a slapback, not a chorus.
    check(ms > 5.5 && ms < 8.0, "resting delay is in the chorus range", buf);
}

//------------------------------------------------------------------------
void testDepthStates()
{
    std::printf("\nDEPTH is two states, and neither clips the clock\n");

    auto span = [](int depth) {
        ChorusEngine e;
        e.prepare(kRate, 512);
        e.setNoiseEnabled(false);
        e.setSettings(make(20, depth, ChorusRange::kSteps - 1));

        const int n = 220500;              // 5 s
        auto in = probeNoise(n);
        std::vector<float> l, r;
        run(e, in, l, r);

        int lo = 1 << 30, hi = 0;
        for (int t = 40000; t < n - 4096; t += 4096)
        {
            const int lag = bestLagIn(in, l, t, 2048, 150, 460);
            lo = std::min(lo, lag);
            hi = std::max(hi, lag);
        }
        return std::pair<int, int>(lo, hi);
    };

    const auto sh = span(ChorusRange::kShallow);
    const auto dp = span(ChorusRange::kDeep);

    char buf[200];
    std::snprintf(buf, sizeof buf, "SHALLOW %.2f-%.2f ms, DEEP %.2f-%.2f ms",
                  sh.first * 1000.0 / kRate, sh.second * 1000.0 / kRate,
                  dp.first * 1000.0 / kRate, dp.second * 1000.0 / kRate);
    check((dp.second - dp.first) > (sh.second - sh.first) * 1.8,
          "DEEP sweeps considerably further than SHALLOW", buf);

    // The whole reason those two values were chosen: the driver clamps at
    // 10-100 kHz, and a setting that reaches the clamp has its modulation
    // FLATTENED for part of every cycle. 5.12 ms is the shortest delay a
    // 1024-stage line can produce at 100 kHz, so a sweep that gets there is
    // sitting on the ceiling.
    const double shortestMs = dp.first * 1000.0 / kRate;
    std::snprintf(buf, sizeof buf,
                  "shortest delay reached %.2f ms (the clamp bites at 5.12)",
                  shortestMs);
    check(shortestMs > 5.2, "DEEP never reaches the clock ceiling", buf);
}

//------------------------------------------------------------------------
// The delay of one output over time, as a track that can be correlated with
// the other one's.
std::vector<int> delayTrack(ChorusEngine& e, const std::vector<float>& in,
                            bool rightChannel, int n)
{
    std::vector<float> l, r;
    run(e, in, l, r);
    const std::vector<float>& sig = rightChannel ? r : l;
    std::vector<int> track;
    for (int t = 44100; t < n - 2048; t += 512)
        track.push_back(bestLagIn(in, sig, t, 1024, 150, 460));
    return track;
}

double correlation(const std::vector<int>& a, const std::vector<int>& b)
{
    const size_t n = std::min(a.size(), b.size());
    if (n < 4) return 0.0;
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= double(n); mb /= double(n);
    double num = 0.0, da = 0.0, db = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double x = a[i] - ma, y = b[i] - mb;
        num += x * y; da += x * x; db += y * y;
    }
    if (da <= 0.0 || db <= 0.0) return 0.0;
    return num / std::sqrt(da * db);
}

//------------------------------------------------------------------------
void testModeIsPhase()
{
    std::printf("\nMODE sets the phase between the two voices\n");

    const int n = 300000;
    auto in = probeNoise(n);

    auto corrAt = [&](int mode) {
        ChorusEngine e1, e2;
        e1.prepare(kRate, 512); e1.setNoiseEnabled(false);
        e1.setSettings(make(24, ChorusRange::kDeep, mode));
        e2.prepare(kRate, 512); e2.setNoiseEnabled(false);
        e2.setSettings(make(24, ChorusRange::kDeep, mode));
        return correlation(delayTrack(e1, in, false, n),
                           delayTrack(e2, in, true, n));
    };

    const double vib = corrAt(0);
    const double mid = corrAt(ChorusRange::kSteps / 2);
    const double cho = corrAt(ChorusRange::kSteps - 1);

    char buf[200];
    std::snprintf(buf, sizeof buf, "VIB %+.2f, middle %+.2f, CHORUS %+.2f",
                  vib, mid, cho);
    check(vib > 0.8, "at VIB the two voices move together", buf);
    check(cho < -0.6, "at CHORUS they move in opposition", buf);
    check(std::abs(mid) < 0.6, "the middle is a quarter cycle apart", buf);
}

//------------------------------------------------------------------------
void testFullyWet()
{
    std::printf("\nNeither output ever carries the dry signal\n");

    const int n = 65536;
    auto in = sine(700.0, n);

    // Checked across the whole of MODE, both ends included. This is the
    // property the plugin is named for, and the previous design broke it: MODE
    // used to crossfade output B to the untouched input, so at the CHORUS end
    // one whole channel WAS the dry signal, bit for bit.
    bool allWet = true;
    double worst = 1e30;
    int worstMode = 0;
    for (int mode = 0; mode < ChorusRange::kSteps; mode += 8)
    {
        ChorusEngine e;
        e.prepare(kRate, 512);
        e.setNoiseEnabled(false);
        e.setSettings(make(36, ChorusRange::kShallow, mode));
        std::vector<float> l, r;
        run(e, in, l, r);

        double dl = 0.0, dr = 0.0;
        for (int i = 20000; i < n; ++i)
        {
            dl = std::max(dl, std::abs(double(l[size_t(i)]) - in[size_t(i)]));
            dr = std::max(dr, std::abs(double(r[size_t(i)]) - in[size_t(i)]));
        }
        const double closest = std::min(dl, dr);
        if (closest < worst) { worst = closest; worstMode = mode; }
        if (closest < 0.05) allWet = false;
    }
    char buf[160];
    std::snprintf(buf, sizeof buf,
                  "closest either output came to the input was %.3f, at MODE %d",
                  worst, worstMode);
    check(allWet, "every MODE position is fully wet on both outputs", buf);
}

//------------------------------------------------------------------------
void testMonoFold()
{
    std::printf("\nCHORUS loses its movement in mono, VIB keeps it\n");

    // What antiphase cancels is the MOVEMENT, not the level.
    //
    // The first version of this test measured how much the mono sum's LEVEL
    // wandered with a 3 kHz sine, and got the answer backwards - 29 dB at VIB
    // against 2 dB at CHORUS. That is real but it is not cancellation: at VIB
    // the two voices are nearly identical, so component tolerance alone puts a
    // slow comb across them and the level heaves; at CHORUS the two delays are
    // so far apart that the notches race past faster than the window and
    // average out. Neither number says anything about the sweep.
    //
    // So measure the sweep itself. Track the delay of the mono sum: at VIB the
    // sum is one voice and its delay swings the full DEEP range, while at
    // CHORUS the two voices pull opposite ways and the sum has no single delay
    // that moves.
    auto sumSweep = [](int mode) {
        ChorusEngine e;
        e.prepare(kRate, 512);
        e.setNoiseEnabled(false);
        e.setSettings(make(20, ChorusRange::kDeep, mode));

        const int n = 300000;
        auto in = probeNoise(n);
        std::vector<float> l, r;
        run(e, in, l, r);

        std::vector<float> mono(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            mono[size_t(i)] = 0.5f * (l[size_t(i)] + r[size_t(i)]);

        int lo = 1 << 30, hi = 0;
        for (int t = 60000; t < n - 4096; t += 4096)
        {
            const int lag = bestLagIn(in, mono, t, 2048, 150, 460);
            lo = std::min(lo, lag);
            hi = std::max(hi, lag);
        }
        return (hi - lo) * 1000.0 / kRate;
    };

    const double vibMs = sumSweep(0);
    const double choMs = sumSweep(ChorusRange::kSteps - 1);
    char buf[200];
    std::snprintf(buf, sizeof buf,
                  "mono sum's delay sweeps %.2f ms at VIB, %.2f ms at CHORUS",
                  vibMs, choMs);
    check(vibMs > 2.0, "VIB survives the fold to mono", buf);
    check(choMs < vibMs * 0.5, "CHORUS loses most of its movement in mono", buf);
}

//------------------------------------------------------------------------
void testSpeedSetsRate()
{
    std::printf("\nSPEED sets the rate\n");

    // Track how the DELAY moves rather than trusting the knob: the LFO is
    // smoothed and glided, and this is what actually reaches the clock.
    auto measured = [](int speed) {
        ChorusEngine e;
        e.prepare(kRate, 512);
        e.setNoiseEnabled(false);
        e.setSettings(make(speed, ChorusRange::kDeep, ChorusRange::kSteps - 1));

        // Six full cycles, so a slow sweep gets a real measurement, capped so
        // the fastest settings do not run for ever.
        const double hz = ChorusRange::speedHz(speed);
        int n = int(6.0 * kRate / hz) + 88200;
        n = std::min(n, 1600000);

        auto in = probeNoise(n);
        std::vector<float> l, r;
        run(e, in, l, r);

        std::vector<int> lags;
        const int hop = 1024;
        for (int t = 44100; t < n - 2048; t += hop)
            lags.push_back(bestLagIn(in, l, t, 1024, 150, 460));

        return dominantHz(lags, kRate / hop, 0.03, 14.0);
    };

    struct { int step; } cases[] = { {16}, {36}, {52} };
    for (auto c : cases)
    {
        const double want = ChorusRange::speedHz(c.step);
        const double got = measured(c.step);
        char buf[160];
        std::snprintf(buf, sizeof buf, "step %d: want %.2f Hz, measured %.2f Hz",
                      c.step, want, got);
        check(std::abs(got - want) < want * 0.15 + 0.02,
              "LFO rate follows SPEED", buf);
    }
}

//------------------------------------------------------------------------
void testBandwidthTracksClock()
{
    std::printf("\nBandwidth tracks the clock\n");

    // The delay line's usable band is half its clock, and the clock moves with
    // the LFO - so a high tone must get louder and quieter across the sweep.
    // A fixed lowpass over a swept delay would not do this at all.
    ChorusEngine e;
    e.prepare(kRate, 512);
    e.setNoiseEnabled(false);
    e.setSettings(make(24, ChorusRange::kDeep, 0));   // VIB

    const int n = 441000;
    auto in = sine(9000.0, n, 0.25);
    std::vector<float> l, r;
    run(e, in, l, r);

    double lo = 1e30, hi = 0.0;
    for (int t = 60000; t < n - 4096; t += 4096)
    {
        const double v = rms(l, t, t + 4096);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    const double swing = dB(hi) - dB(lo);
    char buf[128];
    std::snprintf(buf, sizeof buf, "9 kHz swings %.2f dB across the sweep", swing);
    check(swing > 0.5, "the top end moves with the clock", buf);
}

//------------------------------------------------------------------------
void testNoiseFloor()
{
    std::printf("\nNoise floor\n");
    ChorusEngine e;
    e.prepare(kRate, 512);
    e.setSettings(make(36, ChorusRange::kShallow, 0));   // VIB

    const int n = 132300;
    std::vector<float> in(static_cast<size_t>(n), 0.0f);
    std::vector<float> l, r;
    run(e, in, l, r);

    const double floorDb = dB(rms(l, 40000, n));
    char buf[128];
    std::snprintf(buf, sizeof buf, "%.1f dBFS at rest", floorDb);
    // Present, and not scrubbed: the hardware hisses. Loud enough to hear on a
    // solo'd wet signal, quiet enough not to be the loudest thing in a mix.
    check(floorDb > -110.0 && floorDb < -60.0,
          "hiss is present but not obtrusive", buf);
}

//------------------------------------------------------------------------
void testMonoSum()
{
    std::printf("\nInput is summed to mono\n");

    // The old version of this checked that a left-only input came out halved
    // on the dry output. There is no dry output any more, so it checked the
    // amplitude of a wet voice against an arithmetic prediction and failed by
    // the amount the compander had moved it.
    //
    // The property that actually matters is simpler and is exact: a signal in
    // the left channel and the same signal in the right channel sum to the
    // same mono, so the plugin must produce byte-for-byte the same output for
    // either. If it were listening to one channel, or weighting them
    // differently, these two runs would differ.
    const int n = 32768;
    auto tone = sine(700.0, n);
    std::vector<float> zero(static_cast<size_t>(n), 0.0f);

    ChorusEngine a, b;
    a.prepare(kRate, 512); a.setNoiseEnabled(false);
    a.setSettings(make(36, ChorusRange::kShallow, ChorusRange::kSteps - 1));
    b.prepare(kRate, 512); b.setNoiseEnabled(false);
    b.setSettings(make(36, ChorusRange::kShallow, ChorusRange::kSteps - 1));

    std::vector<float> al(static_cast<size_t>(n)), ar(static_cast<size_t>(n));
    std::vector<float> bl(static_cast<size_t>(n)), br(static_cast<size_t>(n));
    a.processStereo(tone.data(), zero.data(), al.data(), ar.data(), n);
    b.processStereo(zero.data(), tone.data(), bl.data(), br.data(), n);

    double worst = 0.0;
    for (int i = 0; i < n; ++i)
    {
        worst = std::max(worst, std::abs(double(al[size_t(i)]) - bl[size_t(i)]));
        worst = std::max(worst, std::abs(double(ar[size_t(i)]) - br[size_t(i)]));
    }
    char buf[160];
    std::snprintf(buf, sizeof buf,
                  "left-only and right-only inputs differ by %.2e at the output", worst);
    check(worst < 1e-9, "either channel alone gives the same result", buf);

    // ... and it really is a sum rather than a pick: the same tone in BOTH
    // channels must come out louder than in one alone.
    ChorusEngine c;
    c.prepare(kRate, 512); c.setNoiseEnabled(false);
    c.setSettings(make(36, ChorusRange::kShallow, ChorusRange::kSteps - 1));
    std::vector<float> cl(static_cast<size_t>(n)), cr(static_cast<size_t>(n));
    c.processStereo(tone.data(), tone.data(), cl.data(), cr.data(), n);

    double one = 0.0, both = 0.0;
    for (int i = 4000; i < n; ++i)
    {
        one = std::max(one, std::abs(double(al[size_t(i)])));
        both = std::max(both, std::abs(double(cl[size_t(i)])));
    }
    std::snprintf(buf, sizeof buf, "one channel %.3f, both channels %.3f", one, both);
    check(both > one * 1.5, "both channels together are louder than one", buf);
}

//------------------------------------------------------------------------
void testStateGrid()
{
    std::printf("\nState grid rescaling\n");
    // A session written on a different detent count has to come back at the
    // same place on the knob, not the same index. This is the guard that WetEQ
    // shipped without.
    const int from = 17;
    char buf[128];
    const int top = ChorusRange::rescaleStep(from - 1, from);
    std::snprintf(buf, sizeof buf, "index %d of %d -> %d of %d",
                  from - 1, from, top, ChorusRange::kSteps);
    check(top == ChorusRange::kSteps - 1, "the top of an old grid stays at the top", buf);

    const int mid = ChorusRange::rescaleStep(from / 2, from);
    std::snprintf(buf, sizeof buf, "index %d of %d -> %d", from / 2, from, mid);
    check(mid == (ChorusRange::kSteps - 1) / 2, "the centre stays centred", buf);
}

//------------------------------------------------------------------------
} // namespace

int main()
{
    std::printf("chorustest - measuring ChorusEngine at %.0f Hz\n", kRate);
    std::printf("  %d detents per knob, %d of them without Shift\n",
                ChorusRange::kSteps,
                (ChorusRange::kSteps - 1) / ChorusRange::kCoarseStep + 1);

    testRestingDelay();
    testDepthStates();
    testSpeedSetsRate();
    testModeIsPhase();
    testFullyWet();
    testMonoFold();
    testBandwidthTracksClock();
    testNoiseFloor();
    testMonoSum();
    testStateGrid();

    std::printf("\n%s  (%d failure%s)\n",
                failures == 0 ? "ALL PASSED" : "FAILURES", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
