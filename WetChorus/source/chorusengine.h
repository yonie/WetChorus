//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//
// WetChorus's DSP. No VST3 headers, so tools/chorustest can compile and
// measure this directly without loading a plugin host.
//
// Signal flow. The input is summed to mono, because the circuit this models
// has ONE input and makes its stereo image itself:
//
//                                  +-> voice A -> out A (always modulated)
//   in L,R -> sum -> anti-alias ---|
//                                  +-> voice B -> crossfade with dry -> out B
//
// and a voice is the full bucket-brigade chain rather than a delay:
//
//   compress 2:1 -> BBD (clock driven by the LFO) -> reconstruction filter
//                -> expand 1:2 -> noise floor
//
// MODE is what sets the crossfade on output B, and it is one stepped knob
// running the whole way from one setting to the other:
//
//   detent 0    VIB     B fully modulated  - both outputs move, no dry
//   detents 1-15        B is part modulated, part dry
//   detent 16   CHORUS  B fully dry        - the wide setting
//
// Both ends are settings the original hardware has; the ones in between are
// not, and they are the point of the knob. The crossfade is LINEAR rather than
// equal-power, because the two signals are the same source and an equal-power
// law would push the middle of the travel about 3 dB loud.
//
// Nothing is resampled, quantised or band-limited to a house rate. A
// bucket-brigade line is capacitors and a clock - no sample rate and no word
// length - so modelling one with resampler ripple and a quantiser floor would
// put a digital fingerprint on a circuit that never had one. The lo-fi core is
// authentic on WetDelay and WetReverb, which model DIGITAL units. It is not
// authentic here.
//
// There is no channel crosstalk either, and that is a decision rather than an
// omission: the two outputs are meant to be as DIFFERENT as possible, so
// bleeding them together erodes the stereo split that is the entire effect.
// The hardware has no shared stage to bleed through - two amplifiers, two
// speakers.
//------------------------------------------------------------------------

#pragma once

#include "wetcore.h"
#include "bbdcore.h"

namespace Yonie {

//------------------------------------------------------------------------
// Parameter ranges. These are the values the panel prints.
//------------------------------------------------------------------------
namespace ChorusRange {

// SEVENTEEN detents per knob, which is the number of dots the panel art
// actually paints around each control. The art is the spec: the pointer has to
// land on a dot, so the count is counted off the panel rather than chosen
// here, exactly as WetEQ arrived at nine and WetCompressor at twenty-one.
//
// SIXTY-FIVE positions, but you only land on seventeen of them unless you hold
// Shift. 65 = 4*16+1, so every coarse detent is a multiple of four and still
// lands on a painted dot, the centre detent is real - straight up, under the
// printed 5 - and the fine grid is the same size WetEQ settled on.
//
// Must stay in step with KNOB_FRAMES and COARSE_STEP in
// tools/make-assets-wetchorus.py.
constexpr int kSteps = 65;
constexpr int kCoarseStep = 4;
constexpr int kCentreStep = kSteps / 2;      // 32 -> straight up

// 0 .. 10 as printed. The knob index is the truth; this is only for display.
inline double stepScale(int step)
{
    if (step < 0) step = 0;
    if (step > kSteps - 1) step = kSteps - 1;
    return 10.0 * step / (kSteps - 1);
}

// SPEED: exponential, because rate is heard in ratios. A tenth of a hertz is a
// slow drift and ten is a wobble, and the useful chorus range - about half a
// hertz to two - has to sit in the middle of the travel rather than crammed
// into the bottom of a linear sweep.
constexpr double kSpeedMinHz = 0.05;
constexpr double kSpeedMaxHz = 10.0;

inline double speedHz(int step)
{
    const double t = static_cast<double>(step) / (kSteps - 1);
    return kSpeedMinHz * std::pow(kSpeedMaxHz / kSpeedMinHz, t);
}

// DEPTH: two states, not a range. A latching button, the way an amp does a
// bright switch - and the same answer the small pedal version of this circuit
// gives, which has a rate knob and a depth toggle rather than two knobs.
//
// A continuous depth did not earn its place on real material, and its top end
// was unusable anyway: see the clipping note on kBaseClockHz below.
//
// The two values are fractions of the resting clock, chosen so that NEITHER
// end of the sweep reaches the driver's 10-100 kHz limits. Anything that does
// gets flattened against the clamp, and the modulation stops dead for part of
// every cycle.
enum Depth { kShallow = 0, kDeep = 1, kDepthCount = 2 };

constexpr double kShallowSwing = 0.10;   // delay 5.9 - 7.2 ms
constexpr double kDeepSwing    = 0.26;   // delay 5.2 - 8.8 ms

inline double depthSwing(int state)
{
    return state == kDeep ? kDeepSwing : kShallowSwing;
}

// MODE: the LFO PHASE between the two voices, 0 at VIB and half a cycle at
// CHORUS.
//
// This is what makes the plugin fully wet. It used to crossfade the second
// output from its modulated voice to the DRY input, which meant that at the
// CHORUS end one whole channel was the untouched signal - a per-channel mix
// control in all but name, and not what a plugin in this line should be doing.
//
//   VIB     both voices move together      - pitch vibrato, no width, mono-safe
//   middle  a quarter cycle apart          - quadrature, the usual stereo chorus
//   CHORUS  half a cycle apart, opposed    - L sweeps up as R sweeps down, wide
//
// Width now comes from wet against wet instead of wet against dry, and every
// position on the knob is a real setting. Summed to mono the CHORUS end largely
// cancels, which is the signature the line documents.
inline double modePhase(int step)
{
    if (step < 0) step = 0;
    if (step > kSteps - 1) step = kSteps - 1;
    return 0.5 * static_cast<double>(step) / (kSteps - 1);
}

// Where the knobs sit on a fresh insert: 5, 5 and CHORUS.
//
// Both scales open at their centre detent - straight up, under the printed 5 -
// which is where a panel with a 0-10 scale should start. kCentreStep is a real
// position because kSteps is odd, and it is a multiple of kCoarseStep, so a
// fresh insert sits on a painted dot with the pointer vertical.
constexpr int kDefaultSpeed = kCentreStep;    // 5 on the scale, about 0.71 Hz
constexpr int kDefaultDepth = kShallow;       // button out
constexpr int kDefaultMode = kSteps - 1;      // CHORUS, hard right

// Saved state carries step INDICES, so changing kSteps would move every stored
// session unless the grid it was written on is known. WetEQ shipped that bug
// for real - its v1.1.0 read v1.0.0's indices raw and moved every control on
// the panel - so the guard goes in from the FIRST release here rather than the
// second. From version 1 the stream writes the step count and the reader
// rescales.
inline int rescaleStep(int index, int fromSteps)
{
    if (fromSteps <= 1) return 0;
    if (index < 0) index = 0;
    if (index > fromSteps - 1) index = fromSteps - 1;
    if (fromSteps == kSteps) return index;
    return (index * (kSteps - 1)) / (fromSteps - 1);
}

} // namespace ChorusRange

//------------------------------------------------------------------------
// ChorusVoice - one complete bucket-brigade path.
//------------------------------------------------------------------------
class ChorusVoice
{
public:
    void prepare(double sampleRate, uint32_t seed, double tolerance);
    void reset();

    // Resting clock, before the LFO moves it.
    void setBaseClockHz(double hz) { baseClock = hz * clockTrim; }

    // lfo is -1..+1, swing is the fraction of the clock it moves.
    inline float process(float in, float lfo, double swing, bool addNoise);

    const BBDLine& line() const { return bbd; }
    float expanderGainDb() const { return compander.expanderGainDb(); }

private:
    BBDLine bbd;
    Compander compander;

    // Steep either side of the device. Two cascaded 2-pole sections going in,
    // so the clock has nothing above it to fold down, and two coming out whose
    // corner MOVES WITH THE CLOCK - that tracking is the sound.
    Biquad antiAliasA, antiAliasB;
    Biquad reconstructA, reconstructB;
    double reconstructHz = 0.0;

    WhiteNoise noise;
    OnePoleFilter noiseTilt;

    // The reconstruction filter's corner, as a fraction of the DEVICE's
    // Nyquist - which is half the clock, not the clock. At the resting 88 kHz
    // that puts it near 13 kHz, and the LFO walks it between roughly 8 and
    // 18 kHz at full depth. Low enough that nothing folds back, high enough
    // that the wet path is dark rather than muffled.
    static constexpr double kReconstructOfNyquist = 0.30;

    // Where the gain cell runs out. The datasheet's 0.5 % THD is the typical
    // figure at a sensible level, so a normally levelled track sits well under
    // this and a hot one does not.
    static constexpr float kHeadroom = 1.35f;

    // The noise floor, kept audible. Two sources: the flat one is clock
    // feedthrough that survives the reconstruction filter, the tilted one is
    // the device hiss the expander lifts back up. Set 10 dB below what would
    // be obvious on a solo'd wet signal, which is about where the hardware
    // sits against a guitar.
    static constexpr float kClockHiss = 3.0e-6f;
    static constexpr float kDeviceHiss = 5.5e-5f;

    double fs = 44100.0;
    double baseClock = 85000.0;

    // Component tolerance. Real parts are not matched, so the two voices of
    // one unit are never quite the same voice twice: the clock trim moves the
    // resting delay, the filter trim moves the corners.
    double clockTrim = 1.0;
    double filterTrim = 1.0;

    void setReconstruct(double clockHz);
};

//------------------------------------------------------------------------
// ChorusEngine
//------------------------------------------------------------------------
class ChorusEngine
{
public:
    ChorusEngine();

    struct Settings
    {
        // Discrete knob positions, index based, because every WET control is a
        // stepped encoder rather than a continuous pot.
        int speed = ChorusRange::kDefaultSpeed;
        int depth = ChorusRange::kDefaultDepth;   // kShallow / kDeep
        int mode  = ChorusRange::kDefaultMode;

        bool operator==(const Settings& o) const
        {
            return speed == o.speed && depth == o.depth && mode == o.mode;
        }
        bool operator!=(const Settings& o) const { return !(*this == o); }
    };

    void prepare(double hostSampleRate, int maxBlockSize);
    void reset();

    void setSettings(const Settings& s);
    const Settings& settings() const { return current; }

    // Mono in, stereo out. inL and inR are summed; the caller does not have to
    // do it.
    void processStereo(const float* inL, const float* inR,
                       float* outL, float* outR, int numSamples);

    double speedHz() const { return ChorusRange::speedHz(current.speed); }
    double depthSwing() const { return ChorusRange::depthSwing(current.depth); }
    double modePhase() const { return ChorusRange::modePhase(current.mode); }

    // Delay at rest, before the LFO moves it. The tests check this lands in
    // the single-digit milliseconds a chorus lives at.
    float restingDelayMs() const;

    // Test hooks, so the harness can isolate the parts.
    void setNoiseEnabled(bool on) { noiseOn = on; }
    void setToleranceEnabled(bool on);

    // The clock the voices sit at with the LFO centred, giving a 6.5 ms resting
    // delay on a 1024-stage device.
    //
    // Was 88 kHz, which was too close to the driver's 100 kHz ceiling: at the
    // old full depth the LFO asked for 121 kHz, the line clamped, and the
    // modulation FLATTENED for part of every cycle - a sweep of 5.12 ms one way
    // against 9.39 ms the other. It read as "intense" rather than as broken,
    // which is the worst way for a defect to present.
    //
    // At 78.8 kHz the deepest setting asks for 99.3 kHz and never touches the
    // clamp.
    static constexpr double kBaseClockHz = 78769.0;
    static constexpr int kStages = 1024;

private:
    void updateFromSettings();

    Settings current;
    double hostRate = 44100.0;

    ChorusVoice voiceA, voiceB;
    TriangleLFO lfo;

    // Every control moves in jumps, and a jump in a modulation parameter is a
    // click. What glides is the value behind the detent, over a few
    // milliseconds - long enough that nothing steps, short enough that the
    // change still feels instant.
    float phaseNow = 0.5f, phaseTarget = 0.5f;

    // One corner smoother per LFO tap - see the note in processStereo.
    float cornerA = 0.0f, cornerB = 0.0f;
    float swingNow = 0.0f, swingTarget = 0.0f;
    double rateNow = 1.0, rateTarget = 1.0;
    static constexpr double kGlideMs = 25.0;
    float glideCoeff = 0.01f;

    bool noiseOn = true;
    bool toleranceOn = true;

    // Per-voice component tolerance, as in WetEQ and WetCompressor.
    static constexpr double kTolerance = 0.018;
};

//------------------------------------------------------------------------
} // namespace Yonie
