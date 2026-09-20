//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Yonie {

//------------------------------------------------------------------------
static const Steinberg::FUID kWetChorusProcessorUID (0x7E31B4C2, 0x5A0E4F91, 0xC38D7602, 0x14BA9E58);
static const Steinberg::FUID kWetChorusControllerUID (0x2D0F63A7, 0x9C48517E, 0x60B3EA1D, 0x8F572C04);

#define WetChorusVST3Category "Fx|Modulation"

//------------------------------------------------------------------------
// Parameter IDs
//
// Three controls, because the circuit has three: how fast it moves, how far it
// moves, and how the two outputs are fed. There is no mix control because the
// balance across the pair is a property of the circuit rather than a setting.
//------------------------------------------------------------------------
enum WetChorusParams : Steinberg::Vst::ParamID
{
    kSpeedParam  = 0,    // LFO rate, 0..10 as printed
    kDepthParam  = 1,    // how far the clock swings, 0..10 as printed
    kModeParam   = 2,    // VIB .. CHORUS, the whole travel

    // Output-only, for the three LED strips. One IN, because the input is
    // summed to mono; two OUT, because the stereo image is made here.
    kInMeter     = 3,
    kOutMeterL   = 4,
    kOutMeterR   = 5,

    kParamCount  = 6
};

//------------------------------------------------------------------------
} // namespace Yonie
