//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "wetchorusprocessor.h"
#include "wetchoruscontroller.h"
#include "wetchoruscids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "WetChorus"

using namespace Steinberg::Vst;
using namespace Yonie;

//------------------------------------------------------------------------
//  VST Plug-in Entry
//------------------------------------------------------------------------

BEGIN_FACTORY_DEF ("Yonie",
                   "https://github.com/yonie",
                   "mailto:contact@wetvst.com")

	DEF_CLASS2 (INLINE_UID_FROM_FUID(kWetChorusProcessorUID),
				PClassInfo::kManyInstances,
				kVstAudioEffectClass,
				stringPluginName,
				Vst::kDistributable,
				WetChorusVST3Category,
				FULL_VERSION_STR,
				kVstVersionString,
				WetChorusProcessor::createInstance)

	DEF_CLASS2 (INLINE_UID_FROM_FUID (kWetChorusControllerUID),
				PClassInfo::kManyInstances,
				kVstComponentControllerClass,
				stringPluginName "Controller",
				0,
				"",
				FULL_VERSION_STR,
				kVstVersionString,
				WetChorusController::createInstance)

END_FACTORY
