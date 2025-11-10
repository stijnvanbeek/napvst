
#pragma once

// must always come first
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
//------------------------------------------------------------------------

#include "pluginterfaces/vst/ivstcontextmenu.h"

#include <audio/service/audioservice.h>
#include <ControlThread.h>
#include <midiservice.h>
#include <nap/core.h>
#include <renderservice.h>
#include <sdlinputservice.h>
#include <parameter.h>
#include <renderwindow.h>

#include "sdleventconverter.h"
#include "base/source/timer.h"


namespace Steinberg {
namespace Vst {

template <typename T>
class AGainUIMessageController;

class NapPlugin : public SingleComponentEffect, ITimerCallback
{
public:
	//------------------------------------------------------------------------

	NapPlugin ();

	static FUnknown* createInstance (void* /*context*/) { return (IAudioProcessor*)new NapPlugin; }

	//---from IComponent-----------------------
	tresult PLUGIN_API initialize (FUnknown* context) SMTG_OVERRIDE;
	tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	tresult PLUGIN_API setActive (TBool state) SMTG_OVERRIDE;
	tresult PLUGIN_API process (ProcessData& data) SMTG_OVERRIDE;
	tresult PLUGIN_API canProcessSampleSize (int32 symbolicSampleSize) SMTG_OVERRIDE;
	tresult PLUGIN_API setState (IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API getState (IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API setupProcessing (ProcessSetup& newSetup) SMTG_OVERRIDE;
	tresult PLUGIN_API setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
	                                       SpeakerArrangement* outputs,
	                                       int32 numOuts) SMTG_OVERRIDE;

	//---from IEditController-------
	IPlugView* PLUGIN_API createView (const char* name) SMTG_OVERRIDE;
	tresult PLUGIN_API setEditorState (IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API getEditorState (IBStream* state) SMTG_OVERRIDE;
	tresult PLUGIN_API setParamNormalized (ParamID tag, ParamValue value) SMTG_OVERRIDE;
	tresult PLUGIN_API getParamStringByValue (ParamID tag, ParamValue valueNormalized,
	                                          String128 string) SMTG_OVERRIDE;
	tresult PLUGIN_API getParamValueByString (ParamID tag, TChar* string,
	                                          ParamValue& valueNormalized) SMTG_OVERRIDE;
	void onTimer(Timer* timer) SMTG_OVERRIDE;

	//---Interface---------
	OBJ_METHODS (NapPlugin, SingleComponentEffect)
	tresult PLUGIN_API queryInterface (const TUID iid, void** obj) SMTG_OVERRIDE;
	REFCOUNT_METHODS (SingleComponentEffect)

private:
	bool initializeNAP(nap::TaskQueue& mainThreadQueue, nap::utility::ErrorState& errorState);
	void registerParameters(const std::vector<nap::rtti::ObjectPtr<nap::Parameter>>& napParameters);

	int kBypassId = 0;
	bool mBypass = false;
	int mProcessingMode;

	std::unique_ptr<nap::Core> mCore = nullptr;
	nap::Core::ServicesHandle mServices;
	nap::audio::AudioService* mAudioService = nullptr;
	nap::MidiService* mMidiService = nullptr;
	nap::RenderService* mRenderService = nullptr;
	nap::SDLInputService* mInputService = nullptr;
	std::vector<nap::Parameter*> mParameters;
	nap::ControlThread mUpdateThread;

	std::unique_ptr<nap::RenderWindow> mRenderWindow = nullptr;
	Timer* mTimer = nullptr;
	std::unique_ptr<nap::SDLEventConverter> mEventConverter = nullptr;

	nap::Slot<double> mUpdateSlot = { this, &NapPlugin::update };
	void update(double deltaTime);
	std::function<void(double)> mUpdateFunction = [](double){};
};

//------------------------------------------------------------------------
} // namespace Vst
} // namespace Steinberg
