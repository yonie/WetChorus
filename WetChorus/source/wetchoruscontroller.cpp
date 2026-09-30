//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "wetchoruscontroller.h"
#include "weteditor.h"
#include "wetchoruscids.h"
#include "chorusengine.h"
#include "customviewcreator.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <cmath>
#include <cstdio>

using namespace Steinberg;

namespace Yonie {

namespace {

//------------------------------------------------------------------------
// A stepped 0-10 parameter that prints what the panel prints, and prints the
// real-world value beside it so a host's generic view is still useful.
//
// The strings come from the same ChorusRange table the DSP reads, so the
// readout and the sound can never disagree.
//------------------------------------------------------------------------
class ScaleParameter : public Vst::Parameter
{
public:
    enum Kind { kSpeedKind, kDepthKind };

    ScaleParameter(const Vst::TChar* title, Vst::ParamID id, Kind k, int defaultStep)
    : kind(k)
    {
        Vst::ParameterInfo& i = info;
        UString(i.title, str16BufferSize(Vst::String128)).assign(title);
        i.id = id;
        i.stepCount = ChorusRange::kSteps - 1;
        i.defaultNormalizedValue =
            static_cast<double>(defaultStep) / (ChorusRange::kSteps - 1);
        i.unitId = Vst::kRootUnitId;
        i.flags = Vst::ParameterInfo::kCanAutomate;
        setNormalized(i.defaultNormalizedValue);
    }

    static int stepOf(Vst::ParamValue normalized)
    {
        int idx = static_cast<int>(normalized * (ChorusRange::kSteps - 1) + 0.5);
        if (idx < 0) idx = 0;
        if (idx > ChorusRange::kSteps - 1) idx = ChorusRange::kSteps - 1;
        return idx;
    }

    void toString(Vst::ParamValue normalized, Vst::String128 string) const SMTG_OVERRIDE
    {
        const int step = stepOf(normalized);
        char text[64];
        std::snprintf(text, sizeof(text), "%.1f  (%.2f Hz)",
                      ChorusRange::stepScale(step), ChorusRange::speedHz(step));
        UString(string, str16BufferSize(Vst::String128)).fromAscii(text);
    }

    bool fromString(const Vst::TChar* string, Vst::ParamValue& normalized) const SMTG_OVERRIDE
    {
        if (!string)
            return false;
        UString wrapper(const_cast<Vst::TChar*>(string), str16BufferSize(Vst::String128));
        double want = 0.0;
        if (!wrapper.scanFloat(want))
            return false;

        // Snap whatever was typed to the nearest detent on the printed scale.
        int best = 0;
        double bestErr = 1e30;
        for (int i = 0; i < ChorusRange::kSteps; ++i)
        {
            const double err = std::abs(ChorusRange::stepScale(i) - want);
            if (err < bestErr) { bestErr = err; best = i; }
        }
        normalized = static_cast<double>(best) / (ChorusRange::kSteps - 1);
        return true;
    }

private:
    Kind kind;
};

//------------------------------------------------------------------------
// DEPTH. Two states, so it IS a list - a latching button, not a range.
//------------------------------------------------------------------------
class DepthParameter : public Vst::Parameter
{
public:
    DepthParameter(const Vst::TChar* title, Vst::ParamID id)
    {
        Vst::ParameterInfo& i = info;
        UString(i.title, str16BufferSize(Vst::String128)).assign(title);
        i.id = id;
        i.stepCount = ChorusRange::kDepthCount - 1;
        i.defaultNormalizedValue = static_cast<double>(ChorusRange::kDefaultDepth);
        i.unitId = Vst::kRootUnitId;
        i.flags = Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsList;
        setNormalized(i.defaultNormalizedValue);
    }

    void toString(Vst::ParamValue normalized, Vst::String128 string) const SMTG_OVERRIDE
    {
        const bool deep = normalized >= 0.5;
        UString(string, str16BufferSize(Vst::String128))
            .fromAscii(deep ? "DEEP" : "SHALLOW");
    }
};

//------------------------------------------------------------------------
// MODE. Not a list parameter, because it is not a list: it is one continuous
// travel between two named end stops, and every position in between is a real
// setting. A host that showed it as three items would be lying about it.
//------------------------------------------------------------------------
class ModeParameter : public Vst::Parameter
{
public:
    ModeParameter(const Vst::TChar* title, Vst::ParamID id)
    {
        Vst::ParameterInfo& i = info;
        UString(i.title, str16BufferSize(Vst::String128)).assign(title);
        i.id = id;
        i.stepCount = ChorusRange::kSteps - 1;
        i.defaultNormalizedValue =
            static_cast<double>(ChorusRange::kDefaultMode) / (ChorusRange::kSteps - 1);
        i.unitId = Vst::kRootUnitId;
        i.flags = Vst::ParameterInfo::kCanAutomate;
        setNormalized(i.defaultNormalizedValue);
    }

    void toString(Vst::ParamValue normalized, Vst::String128 string) const SMTG_OVERRIDE
    {
        int idx = static_cast<int>(normalized * (ChorusRange::kSteps - 1) + 0.5);
        if (idx < 0) idx = 0;
        if (idx > ChorusRange::kSteps - 1) idx = ChorusRange::kSteps - 1;

        char text[64];
        if (idx == 0)
            std::snprintf(text, sizeof(text), "VIB");
        else if (idx == ChorusRange::kSteps - 1)
            std::snprintf(text, sizeof(text), "CHORUS");
        else
            // modePhase is in CYCLES and tops out at half of one, so degrees
            // is x360 and the readout runs 0 to 180. It used to be x720, which
            // printed "180 deg" at mid-travel where the two voices are really
            // a quarter cycle - 90 degrees - apart.
            std::snprintf(text, sizeof(text), "%d deg",
                          static_cast<int>(ChorusRange::modePhase(idx) * 360.0 + 0.5));
        UString(string, str16BufferSize(Vst::String128)).fromAscii(text);
    }
};

} // namespace

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusController::initialize(FUnknown* context)
{
    tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    registerCustomViews();

    parameters.addParameter(new ScaleParameter(
        reinterpret_cast<const Vst::TChar*>(u"Speed"), kSpeedParam,
        ScaleParameter::kSpeedKind, ChorusRange::kDefaultSpeed));
    parameters.addParameter(new DepthParameter(
        reinterpret_cast<const Vst::TChar*>(u"Depth"), kDepthParam));
    parameters.addParameter(new ModeParameter(
        reinterpret_cast<const Vst::TChar*>(u"Mode"), kModeParam));

    // Meter feeds. Read-only so a host never tries to automate them.
    parameters.addParameter(STR16("Input Meter"), nullptr, 0, 0,
                            Vst::ParameterInfo::kIsReadOnly, kInMeter);
    parameters.addParameter(STR16("Output Meter L"), nullptr, 0, 0,
                            Vst::ParameterInfo::kIsReadOnly, kOutMeterL);
    parameters.addParameter(STR16("Output Meter R"), nullptr, 0, 0,
                            Vst::ParameterInfo::kIsReadOnly, kOutMeterR);

    return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusController::terminate()
{
    return EditControllerEx1::terminate();
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusController::setComponentState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    int32 version = 0;
    if (!streamer.readInt32(version))
        return kResultFalse;

    // Same read as WetChorusProcessor::setState, and it has to stay the same:
    // a host hands the identical stream to both, and a disagreement shows up
    // as a UI that does not match what you hear.
    int32 savedSteps = 0;
    if (!streamer.readInt32(savedSteps) || savedSteps <= 1)
        savedSteps = ChorusRange::kSteps;

    int32 v = 0;
    auto restoreKnob = [&](Vst::ParamID id) {
        if (streamer.readInt32(v))
        {
            const int idx = ChorusRange::rescaleStep(v, savedSteps);
            setParamNormalized(id, static_cast<double>(idx) / (ChorusRange::kSteps - 1));
        }
    };
    auto restoreState = [&](Vst::ParamID id, int count) {
        if (streamer.readInt32(v))
        {
            if (v < 0) v = 0;
            if (v > count - 1) v = count - 1;
            setParamNormalized(id, count > 1 ? static_cast<double>(v) / (count - 1) : 0.0);
        }
    };
    restoreKnob(kSpeedParam);
    restoreState(kDepthParam, ChorusRange::kDepthCount);
    restoreKnob(kModeParam);

    return kResultOk;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API WetChorusController::createView(FIDString name)
{
    if (FIDStringsEqual(name, Vst::ViewType::kEditor))
    {
        auto* editor = new Yonie::WetEditor (this, "view", "wetchoruseditor.uidesc");

        // Discrete zoom steps, as the rest of the line. The assets are baked
        // at 1x, so anything above 125% interpolates and goes soft.
        editor->setAllowedZoomFactors({0.75, 1.0, 1.25});
        return editor;
    }
    return nullptr;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusController::setState(IBStream* /*state*/)
{
    return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetChorusController::getState(IBStream* /*state*/)
{
    return kResultTrue;
}

//------------------------------------------------------------------------
} // namespace Yonie
