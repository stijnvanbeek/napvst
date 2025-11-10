#include "napplugin.h"
#include "version.h" // for versioning

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "base/source/fstreamer.h"

#include <parameternumeric.h>
#include <parameterdropdown.h>
#include <parametergroup.h>
#include <sdlhelpers.h>

#include <cmath>
#include <cstdio>
#include <functional>


// this allows to enable the communication example between again and its controller
#define AGAIN_TEST 1

namespace Steinberg {
namespace Vst {


NapPlugin::NapPlugin ()
{
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::initialize (FUnknown* context)
{
	tresult result = SingleComponentEffect::initialize (context);
	if (result != kResultOk)
		return result;

	//---create Audio In/Out busses------
	// we want a stereo Input and a Stereo Output
	addAudioInput  (STR16 ("Stereo In"),  SpeakerArr::kStereo);
	addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);

	//---create Event In/Out busses (1 bus with only 1 channel)------
	addEventInput (STR16 ("Event In"), 1);

	parameters.addParameter (STR16("Bypass"), nullptr, 1, 0, ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypassId);

	nap::utility::ErrorState error;

	mControlThread.start();
	bool napResult = false;
	std::atomic<bool> napInitialized(false);
	mControlThread.enqueue([&]()
	{
		napResult = initializeNAP(mMainThreadQueue, error);
		napInitialized = true;
	});
	while (!napInitialized)
		mMainThreadQueue.process();
	mMainThreadQueue.process(); // Make sure the queue is empty

	if (!napResult)
		return kResultFalse;

	mEventConverter = std::make_unique<nap::SDLEventConverter>(*mInputService);
	mTimer = Timer::create(this, 1000.f / 60.f);

	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::terminate ()
{
	mTimer->stop();
	mTimer->release();
	auto plugResult = SingleComponentEffect::terminate ();

	mControlThread.enqueue([&]()
	{
		mRenderWindow->onDestroy();
		mRenderWindow = nullptr;
		mServices = nullptr;
		mCore = nullptr;
		mAudioService = nullptr;
		mMidiService = nullptr;
		mRenderService = nullptr;
		mInputService = nullptr;
		mParameters.clear();
	}, true);

	mControlThread.stop();

	return plugResult;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::setActive (TBool state)
{
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::process (ProcessData& data)
{
	// finally the process function
	// In this example there are 4 steps:
	// 1) Read inputs parameters coming from host (in order to adapt our model values)
	// 2) Read inputs events coming from host (we apply a gain reduction depending of the velocity of pressed key)
	// 3) Process the gain of the input buffer to the output buffer
	// 4) Write the new VUmeter value to the output Parameters queue
	//--- Read inputs parameter changes-----------

	if (data.inputParameterChanges)
	{
		int32 numParamsChanged = data.inputParameterChanges->getParameterCount ();
		for (int32 index = 0; index < numParamsChanged; index++)
		{
			Vst::IParamValueQueue* paramQueue =
				data.inputParameterChanges->getParameterData (index);
			if (paramQueue)
			{
				Vst::ParamValue value;
				int32 sampleOffset;
				int32 numPoints = paramQueue->getPointCount ();
				if (paramQueue->getParameterId() == kBypassId)
				{
					if (paramQueue->getPoint (numPoints - 1, sampleOffset, value) == kResultTrue)
						mBypass = (value > 0.5f);
				}

				int paramId = 1; // start from 1, 0 is reserved for bypass
				for (auto& parameter : mParameters)
				{
					if (paramQueue->getParameterId() == paramId++)
					{
						if (paramQueue->getPoint (numPoints - 1, sampleOffset, value) == kResultTrue)
						{
							auto floatParam = rtti_cast<nap::ParameterFloat>(parameter);
							if (floatParam != nullptr)
								mControlThread.enqueue([floatParam, value](){
									floatParam->setValue(nap::math::fit<float>(value, 0.f, 1.f, floatParam->mMinimum, floatParam->mMaximum));
								});
							auto intParam = rtti_cast<nap::ParameterInt>(parameter);
							if (intParam != nullptr)
								mControlThread.enqueue([intParam, value](){
									intParam->setValue(nap::math::fit<float>(value, 0.f, 1.f, intParam->mMinimum, intParam->mMaximum));
								});
							auto optionParam = rtti_cast<nap::ParameterDropDown>(parameter);
							if (optionParam != nullptr)
								mControlThread.enqueue([optionParam, value](){
									optionParam->setSelectedIndex(nap::math::fit<float>(value, 0.f, 1.f, 0, optionParam->mItems.size() - 1));
								});
						}
					}
				}
			}
		}
	}

	// --- Process note events
	auto events = data.inputEvents;
	if (events)
	{
		int32 count = events->getEventCount ();
		for (int32 i = 0; i < count; i++)
		{
			Vst::Event e;
			events->getEvent (i, e);
			switch (e.type)
			{
				case Vst::Event::kNoteOnEvent:
				{
					auto midiEvent = std::make_unique<nap::MidiEvent>(nap::MidiEvent::Type::noteOn, e.noteOn.pitch, e.noteOn.velocity * 127);
					mMidiService->enqueueEvent(std::move(midiEvent));
					break;
				}
				case Vst::Event::kNoteOffEvent:
				{
					auto midiEvent = std::make_unique<nap::MidiEvent>(nap::MidiEvent::Type::noteOff, e.noteOn.pitch, 0);
					mMidiService->enqueueEvent(std::move(midiEvent));
					break;
				}
				default:
					continue;
			}
		}
	}

	//--- Process Audio---------------------
	//--- ----------------------------------
	if (data.numOutputs == 0)
	{
		// nothing to do
		return kResultOk;
	}

	if (data.numSamples > 0)
	{
		if (data.numSamples != mAudioService->getNodeManager().getInternalBufferSize())
			mAudioService->getNodeManager().setInternalBufferSize(data.numSamples);

		// Process Algorithm
		mAudioService->onAudioCallback(data.numInputs > 0 ? data.inputs[0].channelBuffers32 : nullptr, data.outputs[0].channelBuffers32, data.numSamples);
	}

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::setState (IBStream* state)
{
	IBStreamer streamer (state, kLittleEndian);
	int32 savedBypass = 0;
	if (!streamer.readInt32 (savedBypass))
		return kResultFalse;
	mBypass = savedBypass > 0;

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::getState (IBStream* state)
{
	IBStreamer streamer (state, kLittleEndian);

	streamer.writeInt32 (mBypass ? 1 : 0);

	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::setupProcessing (ProcessSetup& newSetup)
{
	// called before the process call, always in a disable state (not active)
	// here we keep a trace of the processing mode (offline,...) for example.
	mAudioService->getNodeManager().setSampleRate(newSetup.sampleRate);
	mAudioService->getNodeManager().setInternalBufferSize(newSetup.maxSamplesPerBlock);

	mProcessingMode = newSetup.processMode;
	return SingleComponentEffect::setupProcessing (newSetup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                    SpeakerArrangement* outputs, int32 numOuts)
{
	auto& nodeManager = mAudioService->getNodeManager();
	nodeManager.setInputChannelCount(numIns);
	nodeManager.setOutputChannelCount(numOuts);

	SingleComponentEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
	return kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::canProcessSampleSize (int32 symbolicSampleSize)
{
	if (symbolicSampleSize == kSample32)
		return kResultTrue;

	// we support double processing
	if (symbolicSampleSize == kSample64)
		return kResultTrue;

	return kResultFalse;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API NapPlugin::createView (const char* name)
{
	return nullptr;
}


//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::setEditorState (IBStream* state)
{
	return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::getEditorState (IBStream* state)
{
	return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::setParamNormalized (ParamID tag, ParamValue value)
{
	// called from host to update our parameters state
	tresult result = SingleComponentEffect::setParamNormalized (tag, value);
	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::getParamStringByValue (ParamID tag, ParamValue valueNormalized,
                                                       String128 string)
{
	return SingleComponentEffect::getParamStringByValue (tag, valueNormalized, string);
}

//------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::getParamValueByString (ParamID tag, TChar* string,
                                                       ParamValue& valueNormalized)
{
	return SingleComponentEffect::getParamValueByString (tag, string, valueNormalized);
}


void NapPlugin::onTimer(Timer *timer)
{

	mControlThread.enqueue([&](){ update(); }, true);
}


//-----------------------------------------------------------------------------
tresult PLUGIN_API NapPlugin::queryInterface (const TUID iid, void** obj)
{
	return SingleComponentEffect::queryInterface (iid, obj);
}

bool NapPlugin::initializeNAP(nap::TaskQueue& mainThreadQueue, nap::utility::ErrorState& errorState)
{
	mCore = std::make_unique<nap::Core>(mainThreadQueue);
	if (!mCore->initializeEngineWithoutProjectInfo(errorState))
		return false;
	mCore->setupPlatformSpecificEnvironment();
	mServices = mCore->initializeServices(errorState);
	if (!mServices->initialized())
		return false;

	mAudioService = mCore->getService<nap::audio::AudioService>();
	mAudioService->getNodeManager().setInputChannelCount(2);
	mAudioService->getNodeManager().setOutputChannelCount(2);

	// std::string app_structure_path = "/Users/stijn/Documents/GitHub/vsttest/data/objects.json";
	// std::string data_dir = "/Users/stijn/Documents/GitHub/vsttest/data/";

	std::string app_structure_path = "/Users/stijn/Documents/GitHub/nap/modules/napaudioadvanced/demo/fmsynth/data/fmsynth.json";
	std::string data_dir = "/Users/stijn/Documents/GitHub/nap/modules/napaudioadvanced/demo/fmsynth/data/";

	// std::string app_structure_path = xstr(APP_STRUCTURE_PATH);
	// std::string data_dir = xstr(DATA_DIR);

	if (nap::utility::fileExists(app_structure_path))
	{
		nap::utility::changeDir(data_dir);
		app_structure_path = nap::utility::getFileName(app_structure_path);
		if (!mCore->getResourceManager()->loadFile(app_structure_path, errorState))
			return false;
		// mCore->getResourceManager()->watchDirectory(data_dir);
	}
	else {
		// std::string app_structure = symbol(APP_STRUCTURE_BINARY);
		// std::vector<nap::rtti::FileLink> fileLinks;
		// if (!mCore->getResourceManager()->loadJSON(app_structure, std::string(), fileLinks, errorState))
		// 	return false;
		return false;
	}

	mAudioService = mCore->getService<nap::audio::AudioService>();
	mMidiService = mCore->getService<nap::MidiService>();
	mRenderService = mCore->getService<nap::RenderService>();
	mInputService = mCore->getService<nap::SDLInputService>();

	auto parameterGroup = mCore->getResourceManager()->findObject<nap::ParameterGroup>("Parameters").get();
	if (parameterGroup != nullptr)
		registerParameters(parameterGroup->mMembers);

	mRenderWindow = std::make_unique<nap::RenderWindow>(*mCore);
	mRenderWindow->mClearColor = { 1.0f, 0.f, 0.f, 1.f };
	if (!mRenderWindow->init(errorState))
		return false;

	return true;
}


void NapPlugin::registerParameters(const std::vector<nap::rtti::ObjectPtr<nap::Parameter>>& napParameters)
{
	auto paramID = kBypassId + 1;
	for (auto& napParameter : napParameters)
	{
 		Vst::TChar paramName[128];

 		if (napParameter->get_type() == RTTI_OF(nap::ParameterFloat))
		{
     		mParameters.emplace_back(napParameter.get());
 			auto napParameterFloat = rtti_cast<nap::ParameterFloat>(napParameter.get());
			Steinberg::Vst::StringConvert::convert(napParameterFloat->getDisplayName(), paramName);
			auto parameter = std::make_unique<Vst::RangeParameter>(paramName, paramID++, STR16(""), napParameterFloat->mMinimum, napParameterFloat->mMaximum, napParameterFloat->mValue);
			parameters.addParameter(parameter.release());
		}

		if (napParameter->get_type() == RTTI_OF(nap::ParameterInt))
		{
     		mParameters.emplace_back(napParameter.get());
			auto napParameterInt = rtti_cast<nap::ParameterInt>(napParameter.get());
			Steinberg::Vst::StringConvert::convert(napParameterInt->getDisplayName(), paramName);
			auto parameter = std::make_unique<Vst::RangeParameter>(paramName, paramID++, STR16(""), napParameterInt->mMinimum, napParameterInt->mMaximum, napParameterInt->mValue, 1.f);
			parameters.addParameter(parameter.release());
		}

		if (napParameter->get_type() == RTTI_OF(nap::ParameterDropDown))
		{
     		mParameters.emplace_back(napParameter.get());
			auto napParameterOptionList = rtti_cast<nap::ParameterDropDown>(napParameter.get());
			Steinberg::Vst::StringConvert::convert(napParameterOptionList->getDisplayName(), paramName);
			auto parameter = std::make_unique<Vst::StringListParameter>(paramName, paramID++, STR16(""));
			Vst::TChar optionName[128];
			for (auto& option : napParameterOptionList->mItems)
			{
				Steinberg::Vst::StringConvert::convert(option, optionName);
				parameter->appendString(optionName);
			}
			parameters.addParameter(parameter.release());
		}
	}
}


void NapPlugin::update()
{
	std::function<void(double)> drawFunc = [&](double deltaTime)
	{
		// ImGui::Begin("NAP", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
		// if (mParameterGUI != nullptr)
		// 	mParameterGUI->show(false);
		// ImGui::NewLine();
		// ImGui::Text(nap::utility::stringFormat("Framerate: %.02f", mCore->getFramerate()).c_str());
		// ImGui::End();
	};
	mCore->update(drawFunc);

	mRenderService->beginFrame();

	// Begin recording the render commands for the main render window
	if (mRenderService->beginRecording(*mRenderWindow))
	{
		// Begin render pass
		mRenderWindow->beginRendering();

		// Render GUI elements
		// mGuiService->draw();

		// Stop render pass
		mRenderWindow->endRendering();

		// End recording
		mRenderService->endRecording();
	}

	// Proceed to next frame
	mRenderService->endFrame();
}


//------------------------------------------------------------------------
} // namespace Vst
} // namespace Steinberg

//------------------------------------------------------------------------
//------------------------------------------------------------------------
//------------------------------------------------------------------------

//------------------------------------------------------------------------
BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

	//---First plug-in included in this factory-------
	// its kVstAudioEffectClass component
	DEF_CLASS2 (INLINE_UID (0xBD58B550, 0xF9E5634E, 0x9D2EFF39, 0xEA0927B3),
				PClassInfo::kManyInstances,					// cardinality  
				kVstAudioEffectClass,						// the component category (do not change this)
				"NapPlugin VST3",							// here the plug-in name (to be changed)
				0,											// single component effects cannot be distributed so this is zero
				"Fx",										// Subcategory for this plug-in (to be changed)
				FULL_VERSION_STR,							// Plug-in version (to be changed)
				kVstVersionString,							// the VST 3 SDK version (do not change this, always use this define)
				Steinberg::Vst::NapPlugin::createInstance)// function pointer called when this component should be instantiated

END_FACTORY
