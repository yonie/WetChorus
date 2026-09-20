//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "chorusengine.h"

#include <algorithm>
#include <cmath>

namespace Yonie {

//------------------------------------------------------------------------
// ChorusVoice
//------------------------------------------------------------------------
void ChorusVoice::prepare(double sampleRate, uint32_t seed, double tolerance)
{
    fs = sampleRate > 0.0 ? sampleRate : 44100.0;

    // Two draws from the same seed, so a voice's clock and its filters are
    // both off by their own small amount rather than by the same one.
    noise.seed(seed);
    clockTrim = 1.0 + tolerance * static_cast<double>(noise.next());
    filterTrim = 1.0 + tolerance * static_cast<double>(noise.next());
    noise.seed(seed ^ 0x5BF03635u);

    bbd.prepare(fs, ChorusEngine::kStages);
    compander.prepare(fs);

    // Anti-alias going in. Fixed, because the INPUT filter in the circuit is a
    // plain passive network - it does not know what the clock is doing. Two
    // cascaded sections make a 4th-order Butterworth slope.
    //
    // It sits ABOVE the reconstruction filter's resting corner on purpose. Put
    // it below and it becomes the narrowest point in the chain, which hides
    // the reconstruction filter moving - and that movement is the sound.
    const double aaHz = 11000.0 * filterTrim;
    antiAliasA.setLowPass(fs, aaHz, 0.5412);
    antiAliasB.setLowPass(fs, aaHz, 1.3066);

    // The noise floor's warm half. A bucket-brigade hiss is not white - the
    // reconstruction filter is sitting on top of it.
    noiseTilt.setCoefficients(fs, 4200.0, OnePoleFilter::Type::LowPass);

    setReconstruct(bbd.currentClockHz());
    reset();
}

//------------------------------------------------------------------------
void ChorusVoice::reset()
{
    bbd.reset();
    compander.reset();
    antiAliasA.reset();
    antiAliasB.reset();
    reconstructA.reset();
    reconstructB.reset();
    noiseTilt.reset();
}

//------------------------------------------------------------------------
// The reconstruction filter has to sit below the device's own Nyquist, and the
// device's Nyquist is half the clock - so when the LFO moves the clock, this
// corner moves with it. That coupling is the single thing that separates a
// modelled bucket brigade from a swept fractional delay: the top end of the
// wet signal breathes in and out with the sweep.
//
// Recomputed only when the clock has moved enough to matter. Two biquads per
// sample would be most of the plugin's CPU for a change nobody can hear.
//------------------------------------------------------------------------
void ChorusVoice::setReconstruct(double clockHz)
{
    // Half the clock IS the device's Nyquist, so the corner is a fraction of
    // clockHz*0.5 - not of clockHz. Getting that wrong put the target at
    // 26 kHz, where Biquad::clampFreq pinned it to 0.45*fs and it stopped
    // moving altogether: the filter was welded shut and the tracking that is
    // the whole point of modelling a clock did nothing. Caught by chorustest,
    // which measured 0.09 dB of movement where there should be several.
    const double target = clockHz * 0.5 * kReconstructOfNyquist * filterTrim;
    if (reconstructHz > 0.0 && std::abs(target - reconstructHz) < reconstructHz * 0.01)
        return;
    reconstructHz = target;
    reconstructA.setLowPass(fs, reconstructHz, 0.5412);
    reconstructB.setLowPass(fs, reconstructHz, 1.3066);
}

//------------------------------------------------------------------------
inline float ChorusVoice::process(float in, float lfo, double swing, bool addNoise)
{
    // The LFO moves the CLOCK, not the delay. Everything else follows from
    // that: a faster clock is a shorter delay AND a higher device Nyquist.
    const double clock = baseClock * (1.0 + swing * static_cast<double>(lfo));
    bbd.setClockHz(clock);
    setReconstruct(bbd.currentClockHz());

    float x = antiAliasA.process(in);
    x = antiAliasB.process(x);

    x = compander.compress(x);
    x = bbdSaturate(x, kHeadroom);
    x = bbd.process(x);

    x = reconstructA.process(x);
    x = reconstructB.process(x);
    x = compander.expand(x);

    if (addNoise)
    {
        // Two sources: a flat one for the clock feedthrough that survives the
        // reconstruction filter, and a low-passed one for the device hiss the
        // expander has just lifted back up. Kept audible on purpose - the
        // hardware hisses, and that hiss is on every record made with one.
        const float flat = noise.next() * kClockHiss;
        const float warm = noiseTilt.process(noise.next()) * kDeviceHiss;
        x += flat + warm;
    }

    return x;
}

//------------------------------------------------------------------------
// ChorusEngine
//------------------------------------------------------------------------
ChorusEngine::ChorusEngine()
{
    updateFromSettings();
}

//------------------------------------------------------------------------
void ChorusEngine::prepare(double hostSampleRate, int /*maxBlockSize*/)
{
    hostRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;

    const double tol = toleranceOn ? kTolerance : 0.0;
    voiceA.prepare(hostRate, 0x1F2E3D4Cu, tol);
    voiceB.prepare(hostRate, 0x6A7B8C9Du, tol);
    voiceA.setBaseClockHz(kBaseClockHz);
    voiceB.setBaseClockHz(kBaseClockHz);

    lfo.prepare(hostRate);

    const double tau = kGlideMs * 0.001 * hostRate;
    glideCoeff = tau > 1.0 ? static_cast<float>(1.0 - std::exp(-1.0 / tau)) : 1.0f;

    updateFromSettings();
    reset();
}

//------------------------------------------------------------------------
void ChorusEngine::reset()
{
    voiceA.reset();
    voiceB.reset();
    lfo.reset();

    // Both voices run off ONE oscillator, as the later revision of the circuit
    // does - one triangle for both modes rather than a separate oscillator
    // each. MODE reads it at a second phase rather than starting a second one.
    lfo.setPhase(0.0);
    cornerA = cornerB = 0.0f;

    phaseNow = phaseTarget;
    swingNow = swingTarget;
    rateNow = rateTarget;
}

//------------------------------------------------------------------------
void ChorusEngine::setSettings(const Settings& s)
{
    if (s == current)
        return;
    current = s;
    updateFromSettings();
}

//------------------------------------------------------------------------
void ChorusEngine::updateFromSettings()
{
    rateTarget = ChorusRange::speedHz(current.speed);
    swingTarget = static_cast<float>(ChorusRange::depthSwing(current.depth));
    phaseTarget = static_cast<float>(ChorusRange::modePhase(current.mode));
}

//------------------------------------------------------------------------
void ChorusEngine::setToleranceEnabled(bool on)
{
    if (toleranceOn == on)
        return;
    toleranceOn = on;
    prepare(hostRate, 0);
}

//------------------------------------------------------------------------
float ChorusEngine::restingDelayMs() const
{
    return voiceA.line().currentDelayMs();
}

//------------------------------------------------------------------------
void ChorusEngine::processStereo(const float* inL, const float* inR,
                                 float* outL, float* outR, int numSamples)
{
    const float corner = lfo.cornerCoefficient();

    for (int i = 0; i < numSamples; ++i)
    {
        // Sum to mono. The circuit has one input and builds the stereo image
        // itself, so a wide source loses its own width here - correct for the
        // material this is for, and stated in the README rather than worked
        // around.
        const float in = 0.5f * (inL[i] + inR[i]);

        swingNow += glideCoeff * (swingTarget - swingNow);
        phaseNow += glideCoeff * (phaseTarget - phaseNow);
        rateNow += glideCoeff * (rateTarget - rateNow);
        lfo.setRateHz(rateNow);
        lfo.advance();

        // ONE oscillator, read at TWO phases. MODE is the offset between them:
        // zero at VIB so both voices move together, half a cycle at CHORUS so
        // one sweeps up while the other sweeps down.
        //
        // Each tap rounds its own reversals, because taps at different phases
        // turn their corners at different moments.
        const float mA = TriangleLFO::roundCorner(lfo.at(0.0), cornerA, corner);
        const float mB = TriangleLFO::roundCorner(lfo.at(phaseNow), cornerB, corner);

        // BOTH outputs are the modulated voice. There is no dry path anywhere
        // in this plugin - the width comes from the two wet voices moving
        // against each other, not from a wet one against a dry one.
        outL[i] = voiceA.process(in, mA, swingNow, noiseOn);
        outR[i] = voiceB.process(in, mB, swingNow, noiseOn);
    }
}

//------------------------------------------------------------------------
} // namespace Yonie
