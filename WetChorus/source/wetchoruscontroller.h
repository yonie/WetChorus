//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace Yonie {

//------------------------------------------------------------------------
class WetChorusController : public Steinberg::Vst::EditControllerEx1
{
public:
    WetChorusController() = default;
    ~WetChorusController() SMTG_OVERRIDE = default;

    static Steinberg::FUnknown* createInstance(void* /*context*/)
    {
        return (Steinberg::Vst::IEditController*)new WetChorusController;
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

    DEFINE_INTERFACES
    END_DEFINE_INTERFACES(Steinberg::Vst::EditControllerEx1)
    DELEGATE_REFCOUNT(Steinberg::Vst::EditControllerEx1)
};

//------------------------------------------------------------------------
} // namespace Yonie
