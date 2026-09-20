//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//
// The parts a bucket-brigade chorus is built out of. No VST3 headers, so
// tools/chorustest can compile and measure this without loading a host.
//
// A bucket-brigade device is an analog shift register: 1024 capacitors, each
// handing its charge to the next one on every tick of a two-phase clock. The
// signal comes out delayed by
//
//     delay = stages / (2 * f_clock)
//
// and NOTHING else about it is fixed. Move the clock and the delay moves, the
// device's own Nyquist moves with it, and so does the top of the audio band.
// That coupling is the whole sound of a BBD chorus and it is the reason this
// file models a CLOCK rather than a delay time: a swept fractional delay with
// a fixed lowpass over it is a different, duller effect.
//
// The chain around the device is not optional either. A 1024-stage line at
// 80 dB signal-to-noise cannot pass music on its own, so every BBD circuit
// ever built wraps it in a compander, and puts a steep filter either side to
// keep the clock out of the audio and the audio out of the clock.
//------------------------------------------------------------------------

#pragma once

#include "wetcore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Yonie {

//------------------------------------------------------------------------
// WhiteNoise - one uncorrelated source. xorshift rather than std::mt19937,
// because this runs per sample on the audio thread.
//------------------------------------------------------------------------
class WhiteNoise
{
public:
    explicit WhiteNoise(uint32_t seed = 0x9E3779B9u) : state(seed | 1u) {}

    inline float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        // [-1, 1)
        return static_cast<float>(static_cast<int32_t>(state)) * 4.6566129e-10f;
    }

    void seed(uint32_t s) { state = s | 1u; }

private:
    uint32_t state;
};

//------------------------------------------------------------------------
// TriangleLFO - a triangle with ROUNDED corners.
//
// The oscillator in one of these amps is an op-amp integrator driven by a
// comparator, and an integrator cannot turn a corner: it slews into the
// reversal over a few milliseconds. A mathematical triangle has an infinitely
// sharp reversal, which is audible as a tick at the top and bottom of the
// sweep once the depth is up.
//
// The rounding is a one-pole smoother on the ramp, so the corner softens and
// the straight middle of the ramp survives - which is what separates a triangle
// sweep from a sine one to begin with.
//------------------------------------------------------------------------
class TriangleLFO
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        setCornerMs(kCornerMs);
        reset();
    }

    void reset()
    {
        phase = 0.0;
        smoothed = 0.0f;
    }

    void setRateHz(double hz)
    {
        if (hz < 0.001) hz = 0.001;
        inc = hz / fs;
    }

    // Start position within the cycle, 0..1. Used to give the two channels
    // slightly different starting points so a fresh insert is not perfectly
    // symmetrical.
    void setPhase(double p) { phase = p - std::floor(p); }

    // Step the oscillator on by one sample. Read it with at().
    inline void advance()
    {
        phase += inc;
        if (phase >= 1.0)
            phase -= 1.0;
    }

    // The raw triangle at a PHASE OFFSET from wherever the oscillator is now,
    // -1 .. +1. Offset is in cycles, so 0.5 is half a cycle - the opposite
    // side of the sweep.
    //
    // One oscillator read twice, rather than two oscillators. Two would have to
    // be kept locked in frequency and would drift; and crossfading between an
    // in-phase and an inverted copy is not the same thing at all, because the
    // halfway point of THAT is silence rather than a quarter cycle.
    inline float at(double offset) const
    {
        double p = phase + offset;
        p -= std::floor(p);
        return (p < 0.5) ? static_cast<float>(p * 4.0 - 1.0)
                         : static_cast<float>(3.0 - p * 4.0);
    }

    // The corner smoother that rounds the reversals lives per TAP rather than
    // in here, because two taps at different phases turn their corners at
    // different times. Callers keep their own state and pass it back.
    static float roundCorner(float raw, float& state, float coeff)
    {
        state += coeff * (raw - state);
        return state;
    }

    float cornerCoefficient() const { return cornerCoeff; }

private:
    void setCornerMs(double ms)
    {
        const double tau = ms * 0.001 * fs;
        cornerCoeff = tau > 1.0 ? static_cast<float>(1.0 - std::exp(-1.0 / tau))
                                : 1.0f;
    }

    // Measured off nothing in particular - it is the smallest rounding that
    // removes the tick at full depth and the largest that leaves the ramp
    // straight enough to still read as a triangle rather than a sine.
    static constexpr double kCornerMs = 6.0;

    double fs = 44100.0;
    double phase = 0.0;
    double inc = 0.0;
    float smoothed = 0.0f;
    float cornerCoeff = 1.0f;
};

//------------------------------------------------------------------------
// Compander - the 2:1 / 1:2 pair wrapped around the delay line.
//
// The device has about 80 dB of signal-to-noise, which is roughly a cassette
// and nowhere near enough for a wet signal sitting under a dry one. So the
// signal is compressed 2:1 going in, sits high above the device's noise floor
// while it is in there, and is expanded 1:2 coming out - which pushes the
// noise back down by as much as it pushes the signal.
//
// It is not transparent and it is not meant to be. The two halves track the
// envelope with real time constants, so on a transient the expander is still
// opening while the signal has already arrived, and the wet path BREATHES.
// That breathing is most of what people hear as "analog chorus".
//
// One detector per half, both on the same law, because the circuit uses a
// matched pair of the same chip.
//------------------------------------------------------------------------
class Compander
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        setTimes(kAttackMs, kReleaseMs);
        reset();
    }

    void reset()
    {
        compEnv = kFloor;
        expEnv = kFloor;
    }

    // 2:1 downwards. Gain is 1/sqrt(env/ref), so a signal 12 dB above the
    // reference comes out 6 dB above it.
    inline float compress(float x)
    {
        compEnv = follow(compEnv, std::fabs(x));
        return x * gainFor(compEnv, true);
    }

    // 1:2 upwards, tracking the compressed signal's own envelope. The gains do
    // not cancel exactly, and that is the point - see the note above.
    inline float expand(float x)
    {
        expEnv = follow(expEnv, std::fabs(x));
        return x * gainFor(expEnv, false);
    }

    // How much gain the expander is applying right now, in dB. The test
    // harness reads this to show the breathing.
    float expanderGainDb() const
    {
        return 20.0f * std::log10(std::max(gainFor(expEnv, false), 1e-9f));
    }

private:
    inline float follow(float env, float x)
    {
        const float k = (x > env) ? attackCoeff : releaseCoeff;
        env += k * (x - env);
        return env < kFloor ? kFloor : env;
    }

    inline float gainFor(float env, bool compressing) const
    {
        // ratio = env / reference, clamped so a silent input does not ask for
        // infinite expansion on the way out.
        float r = env / kReference;
        if (r < 1e-4f) r = 1e-4f;
        const float g = std::sqrt(r);
        return compressing ? (1.0f / g) * kMakeup : g / kMakeup;
    }

    void setTimes(double attackMs, double releaseMs)
    {
        auto coeff = [this](double ms) {
            const double tau = ms * 0.001 * fs;
            return tau > 1.0 ? static_cast<float>(1.0 - std::exp(-1.0 / tau)) : 1.0f;
        };
        attackCoeff = coeff(attackMs);
        releaseCoeff = coeff(releaseMs);
    }

    // Fast enough to catch a transient, slow enough that the release is still
    // opening a few milliseconds later - which is the breath.
    static constexpr double kAttackMs = 1.2;
    static constexpr double kReleaseMs = 55.0;

    // The level the pair is unity at. Below it the compressor lifts, above it
    // it holds down.
    static constexpr float kReference = 0.18f;   // about -15 dBFS
    static constexpr float kFloor = 1.0e-6f;
    static constexpr float kMakeup = 1.0f;

    double fs = 44100.0;
    float compEnv = kFloor, expEnv = kFloor;
    float attackCoeff = 1.0f, releaseCoeff = 1.0f;
};

//------------------------------------------------------------------------
// BBDLine - the delay line itself, addressed by CLOCK rather than by time.
//
// Reads are cubic (Catmull-Rom), not linear. A linear read on a moving tap
// loses high end in proportion to how fast the tap is moving, so the top end
// would dip every time the LFO passed through its steepest part and come back
// at the turnarounds - a wobble that is not in the circuit and does not sound
// like one.
//------------------------------------------------------------------------
class BBDLine
{
public:
    void prepare(double sampleRate, int stageCount)
    {
        fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        stages = stageCount > 1 ? stageCount : 1024;

        // Longest delay the datasheet allows, plus room for the interpolator
        // to look one sample either side.
        const double maxDelaySec = stages / (2.0 * kClockMinHz);
        size = static_cast<int>(maxDelaySec * fs) + 8;
        buffer.assign(static_cast<size_t>(size), 0.0f);
        writePos = 0;
        reset();
    }

    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    // Clock in Hz. Clamped to the range the driver chip actually covers, so a
    // wild modulation depth cannot ask for a delay the buffer does not hold.
    inline void setClockHz(double hz)
    {
        if (hz < kClockMinHz) hz = kClockMinHz;
        if (hz > kClockMaxHz) hz = kClockMaxHz;
        clockHz = hz;
        delaySamples = static_cast<float>(stages / (2.0 * clockHz) * fs);
        const float maxD = static_cast<float>(size - 4);
        if (delaySamples > maxD) delaySamples = maxD;
        if (delaySamples < 2.0f) delaySamples = 2.0f;
    }

    double currentClockHz() const { return clockHz; }
    float currentDelaySamples() const { return delaySamples; }
    float currentDelayMs() const { return static_cast<float>(delaySamples * 1000.0 / fs); }

    // The device's own Nyquist. Everything above it folds back, which is why
    // the circuit filters hard either side - and why the reconstruction filter
    // has to MOVE with the clock rather than sit at a fixed corner.
    double nyquistHz() const { return clockHz * 0.5; }

    inline float process(float in)
    {
        buffer[static_cast<size_t>(writePos)] = in;

        float readPos = static_cast<float>(writePos) - delaySamples;
        while (readPos < 0.0f)
            readPos += static_cast<float>(size);

        const int i1 = static_cast<int>(readPos);
        const float frac = readPos - static_cast<float>(i1);
        const int i0 = (i1 - 1 + size) % size;
        const int i2 = (i1 + 1) % size;
        const int i3 = (i1 + 2) % size;

        const float y0 = buffer[static_cast<size_t>(i0)];
        const float y1 = buffer[static_cast<size_t>(i1 % size)];
        const float y2 = buffer[static_cast<size_t>(i2)];
        const float y3 = buffer[static_cast<size_t>(i3)];

        // Catmull-Rom
        const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c = -0.5f * y0 + 0.5f * y2;
        const float out = ((a * frac + b) * frac + c) * frac + y1;

        if (++writePos >= size)
            writePos = 0;

        return out;
    }

    // Datasheet clock range for the driver, and the reason the delay works out
    // between about 5 and 51 ms on a 1024-stage device.
    static constexpr double kClockMinHz = 10000.0;
    static constexpr double kClockMaxHz = 100000.0;

private:
    std::vector<float> buffer;
    double fs = 44100.0;
    int stages = 1024;
    int size = 1;
    int writePos = 0;
    double clockHz = 85000.0;
    float delaySamples = 64.0f;
};

//------------------------------------------------------------------------
// BBDSaturation - the gain cell running out of headroom.
//
// The datasheet says 0.5% THD typical at 0.78 Vrms in. That is the TYPICAL
// figure at a sensible level, not a ceiling: drive the device harder and each
// bucket has less room to hand its charge on, so distortion climbs steeply
// rather than gracefully. Asymmetric, because the cell is single-ended.
//------------------------------------------------------------------------
inline float bbdSaturate(float x, float headroom)
{
    const float t = x / headroom;
    // tanh for the negative half, a slightly earlier knee for the positive one
    const float k = (t > 0.0f) ? 1.18f : 1.0f;
    return headroom * std::tanh(t * k) / k;
}

//------------------------------------------------------------------------
} // namespace Yonie
