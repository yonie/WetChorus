//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "chorusengine.h"
#include "wetchoruscids.h"
#include <atomic>

namespace Yonie {

//------------------------------------------------------------------------
class WetChorusProcessor : public Steinberg::Vst::AudioEffect
{
public:
    WetChorusProcessor();
    ~WetChorusProcessor() SMTG_OVERRIDE;

    static Steinberg::FUnknown* createInstance(void* /*context*/)
    {
        return (Steinberg::Vst::IAudioProcessor*)new WetChorusProcessor;
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

protected:
    void applyParameter(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

    ChorusEngine engine;
    ChorusEngine::Settings pending;

    // Three peak meters, one per painted strip. IN reads the mono sum, because
    // that is what the circuit is actually fed.
    std::atomic<float> inPeak{0.0f};
    std::atomic<float> outPeakL{0.0f}, outPeakR{0.0f};
    float oldIn = 0.0f, oldOutL = 0.0f, oldOutR = 0.0f;

    static constexpr float kMeterDecay = 0.9995f;

    void updatePeak(float sample, std::atomic<float>& peak);
};

//------------------------------------------------------------------------
} // namespace Yonie
