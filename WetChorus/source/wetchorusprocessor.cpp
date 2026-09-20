//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "wetchorusprocessor.h"
#include "wetchoruscids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;

namespace Yonie {

//------------------------------------------------------------------------
WetChorusProcessor::WetChorusProcessor()
{
    setControllerClass(kWetChorusControllerUID);
}

//------------------------------------------------------------------------
WetChorusProcessor::~WetChorusProcessor() {}

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusProcessor::initialize(FUnknown* context)
{
    tresult result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    // A stereo input even though the circuit is mono in: the plugin has to
    // load on a stereo track, and the engine sums what it is given. Saying
    // "mono in" here would make hosts wrap it or refuse it.
    addAudioInput(STR16("Stereo In"), Steinberg::Vst::SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);

    return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusProcessor::terminate()
{
    return AudioEffect::terminate();
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusProcessor::setActive(TBool state)
{
    if (state)
    {
        engine.reset();
        inPeak = outPeakL = outPeakR = 0.0f;
        oldIn = oldOutL = oldOutR = 0.0f;
    }
    return AudioEffect::setActive(state);
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusProcessor::setupProcessing(Vst::ProcessSetup& newSetup)
{
    engine.prepare(newSetup.sampleRate, newSetup.maxSamplesPerBlock);
    return AudioEffect::setupProcessing(newSetup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusProcessor::canProcessSampleSize(int32 symbolicSampleSize)
{
    if (symbolicSampleSize == Vst::kSample32)
        return kResultTrue;
    return kResultFalse;
}

//------------------------------------------------------------------------
// Normalised parameter value -> discrete knob position.
static int toStep(Steinberg::Vst::ParamValue normalized, int stepCount)
{
    if (stepCount <= 1)
        return 0;
    int idx = static_cast<int>(normalized * (stepCount - 1) + 0.5);
    if (idx < 0) idx = 0;
    if (idx > stepCount - 1) idx = stepCount - 1;
    return idx;
}

//------------------------------------------------------------------------
void WetChorusProcessor::applyParameter(Vst::ParamID id, Vst::ParamValue value)
{
    switch (id)
    {
        case kSpeedParam: pending.speed = toStep(value, ChorusRange::kSteps); break;
        // DEPTH is a two-state button, not a knob: it has never had more than
        // two positions, so it is read straight and must never be rescaled
        // against the knob grid.
        case kDepthParam: pending.depth = toStep(value, ChorusRange::kDepthCount); break;
        case kModeParam:  pending.mode  = toStep(value, ChorusRange::kSteps); break;
        default: break;
    }
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusProcessor::process(Vst::ProcessData& data)
{
    //--- parameter changes ------------------------------------------------
    if (data.inputParameterChanges)
    {
        const int32 numChanged = data.inputParameterChanges->getParameterCount();
        for (int32 index = 0; index < numChanged; ++index)
        {
            if (auto* queue = data.inputParameterChanges->getParameterData(index))
            {
                const int32 numPoints = queue->getPointCount();
                if (numPoints <= 0)
                    continue;
                Vst::ParamValue value;
                int32 sampleOffset;
                // Last point in the block. The engine glides rate, depth and
                // the crossfade itself, so sample-accurate application would
                // buy nothing.
                if (queue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
                    applyParameter(queue->getParameterId(), value);
            }
        }
        engine.setSettings(pending);
    }

    //--- audio ------------------------------------------------------------
    if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples <= 0)
        return kResultOk;

    Vst::AudioBusBuffers& input = data.inputs[0];
    Vst::AudioBusBuffers& output = data.outputs[0];

    if (input.numChannels < 2 || output.numChannels < 2)
    {
        for (int32 c = 0; c < output.numChannels; ++c)
            std::memset(output.channelBuffers32[c], 0,
                        data.numSamples * sizeof(Vst::Sample32));
        output.silenceFlags = ((uint64)1 << output.numChannels) - 1;
        return kResultOk;
    }

    float* inL = input.channelBuffers32[0];
    float* inR = input.channelBuffers32[1];
    float* outL = output.channelBuffers32[0];
    float* outR = output.channelBuffers32[1];

    // IN reads the MONO SUM, not the left channel. That is what the circuit is
    // fed, and it is also the honest reading: on a wide stereo source the sum
    // is quieter than either channel, and a meter showing one channel would
    // hide that the width has already gone.
    for (int32 i = 0; i < data.numSamples; ++i)
        updatePeak(0.5f * (inL[i] + inR[i]), inPeak);

    engine.processStereo(inL, inR, outL, outR, data.numSamples);

    for (int32 i = 0; i < data.numSamples; ++i)
    {
        updatePeak(outL[i], outPeakL);
        updatePeak(outR[i], outPeakR);
    }

    //--- meters -----------------------------------------------------------
    if (data.outputParameterChanges)
    {
        auto sendMeter = [&](Vst::ParamID id, float value, float& oldValue) {
            if (oldValue != value)
            {
                int32 index = 0;
                if (auto* queue = data.outputParameterChanges->addParameterData(id, index))
                {
                    int32 pointIndex = 0;
                    queue->addPoint(0, value, pointIndex);
                }
                oldValue = value;
            }
        };

        sendMeter(kInMeter, inPeak.load(), oldIn);
        sendMeter(kOutMeterL, outPeakL.load(), oldOutL);
        sendMeter(kOutMeterR, outPeakR.load(), oldOutR);
    }

    output.silenceFlags = 0;
    return kResultOk;
}

//------------------------------------------------------------------------
void WetChorusProcessor::updatePeak(float sample, std::atomic<float>& peak)
{
    const float absSample = std::abs(sample);
    const float currentPeak = peak.load();
    if (absSample > currentPeak)
        peak.store(absSample);                    // attack: instant
    else
        peak.store(currentPeak * kMeterDecay);    // decay: exponential
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusProcessor::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    int32 version = 0;
    if (!streamer.readInt32(version))
        return kResultFalse;

    // The grid the knob indices were written on. Written from version 1, so
    // there is no version of this plugin whose state has to be guessed at -
    // which is the whole lesson from what WetEQ shipped.
    int32 savedSteps = 0;
    if (!streamer.readInt32(savedSteps) || savedSteps <= 1)
        savedSteps = ChorusRange::kSteps;

    ChorusEngine::Settings s;
    int32 v = 0;
    auto rdKnob = [&](int& dst) {
        if (streamer.readInt32(v))
            dst = ChorusRange::rescaleStep(v, savedSteps);
    };
    // DEPTH is not on the knob grid - two states, read straight and clamped.
    auto rdState = [&](int& dst, int count) {
        if (streamer.readInt32(v))
        {
            if (v < 0) v = 0;
            if (v > count - 1) v = count - 1;
            dst = v;
        }
    };
    rdKnob(s.speed);
    rdState(s.depth, ChorusRange::kDepthCount);
    rdKnob(s.mode);

    pending = s;
    engine.setSettings(s);
    return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusProcessor::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    // Version, then the step count the indices below are counted on, then the
    // indices. Never write a bare index: a later build with a different detent
    // count would read this session and move every control on the panel.
    streamer.writeInt32(1);
    streamer.writeInt32(ChorusRange::kSteps);

    const ChorusEngine::Settings& s = engine.settings();
    streamer.writeInt32(s.speed);
    streamer.writeInt32(s.depth);
    streamer.writeInt32(s.mode);

    return kResultOk;
}

//------------------------------------------------------------------------
} // namespace Yonie
