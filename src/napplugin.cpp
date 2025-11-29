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


namespace Steinberg
{

	namespace Vst
	{
		NapPlugin::NapPlugin ()
		{
		}


		tresult PLUGIN_API NapPlugin::initialize (FUnknown* context)
		{
			tresult result = SingleComponentEffect::initialize (context);
			if (result != kResultOk)
				return result;

			// we want a stereo Input and a Stereo Output
			addAudioInput  (STR16 ("Stereo In"),  SpeakerArr::kStereo);
			addAudioOutput (STR16 ("Stereo Out"), SpeakerArr::kStereo);

			// One single channel event bus
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

			mEventConverter = std::make_unique<nap::SDLEventConverter>(*mSDLInputService);
			mTimer = Timer::create(this, 1000.f / 60.f);

			mControlThread.connectPeriodicTask(mControlSlot);

			return result;
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
			mInputService = mCore->getService<nap::InputService>();
			mSDLInputService = mCore->getService<nap::SDLInputService>();
			mGuiService = mCore->getService<nap::IMGuiService>();

			auto parameterGroup = mCore->getResourceManager()->findObject<nap::ParameterGroup>("Parameters").get();
			if (parameterGroup != nullptr)
				registerParameters(parameterGroup->mMembers);

			mParameterGUI = std::make_unique<nap::ParameterGUI>(*mCore);
			mParameterGUI->mParameterGroup = parameterGroup;
			if (!mParameterGUI->init(errorState))
				return false;

			mInitialized = true;

			return true;
		}


		tresult PLUGIN_API NapPlugin::terminate ()
		{
			if (!mInitialized)
				return kResultOk;
			mInitialized = false;

			mTimer->stop();
			mTimer->release();

			mControlThread.disconnectPeriodicTask(mControlSlot);
			nap::Logger::info("disconnected periodic task");

			std::atomic<bool> napTerminated(false);
			mControlThread.enqueue([&]()
			{
				mParameterGUI->onDestroy();
				mParameterGUI = nullptr;
				mServices = nullptr;
				mCore = nullptr;
				napTerminated = true;
			});
			while (!napTerminated)
				mMainThreadQueue.process();
			mMainThreadQueue.process();

			mControlThread.stop();

			auto plugResult = SingleComponentEffect::terminate ();
			return plugResult;
		}


		void NapPlugin::onTimer(Timer *timer)
		{
			if (mView == nullptr || !mView->isAttached())
				return;

			SDL_Event event;
			while (mSDLPollerClient.poll(&event))
			{
				// Forward if we're not capturing the mouse in the GUI and it's a pointer event
				if (mEventConverter->isMouseEvent(event))
				{
					nap::InputEventPtr input_event = mEventConverter->translateMouseEvent(event);
					if (input_event == nullptr)
						continue;

					{
						std::lock_guard<std::mutex> lock(mMutex);
						ImGuiContext* ctx = mGuiService->processInputEvent(*input_event);
						if (ctx != nullptr && !mGuiService->isCapturingMouse(ctx))
						{
						}
					}
				}

				// Forward if we're not capturing the keyboard in the GUI and it's a key event
				else if (mEventConverter->isKeyEvent(event))
				{
					nap::InputEventPtr input_event = mEventConverter->translateKeyEvent(event);
					if (input_event == nullptr)
						continue;

					{
						std::lock_guard<std::mutex> lock(mMutex);
						ImGuiContext* ctx = mGuiService->processInputEvent(*input_event);
						if (ctx != nullptr && !mGuiService->isCapturingKeyboard(ctx))
						{
						}
					}
				}

				// Always forward controller events
				else if (mEventConverter->isControllerEvent(event))
				{
					nap::InputEventPtr input_event = mEventConverter->translateControllerEvent(event);
					if (input_event != nullptr)
					{
					}
				}

				// Always forward window events
				else if (mEventConverter->isWindowEvent(event))
				{
					// Quit when request to close
					if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
					{
						std::lock_guard<std::mutex> lock(mMutex);
						// mControlThread.enqueue([&](){ mRenderWindow->hide(); });
					}

					nap::WindowEventPtr window_event = mEventConverter->translateWindowEvent(event);
					if (window_event != nullptr)
					{
					}
				}
			}
			mMainThreadQueue.process();
		}


		void NapPlugin::control(double deltaTime)
		{
			// Begin recording the render commands for the main render window
			if (mView != nullptr && mView->isAttached())
			{
				std::lock_guard<std::mutex> lock(mMutex);

				std::function<void(double)> drawFunc = [&](double deltaTime)
				{
					ImGui::Begin("NAP", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
					if (mParameterGUI != nullptr)
						mParameterGUI->show(false);
					ImGui::NewLine();
					ImGui::Text(nap::utility::stringFormat("Framerate: %.02f", mCore->getFramerate()).c_str());
					ImGui::End();
				};
				mCore->update(drawFunc);

				mRenderService->beginFrame();

				if (mRenderService->beginRecording(*mView->getRenderWindow()))
				{
					// Begin render pass
					mView->getRenderWindow()->beginRendering();

					// Render GUI elements
					mGuiService->draw();

					// Stop render pass
					mView->getRenderWindow()->endRendering();

					// End recording
					mRenderService->endRecording();
				}

				// Proceed to next frame
				mRenderService->endFrame();
			}
		}


		tresult PLUGIN_API NapPlugin::process (ProcessData& data)
		{
			// Process parameters
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

			// Process note events
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

			// Process audio
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


		tresult PLUGIN_API NapPlugin::setActive (TBool state)
		{
			return kResultOk;
		}


		tresult PLUGIN_API NapPlugin::setState (IBStream* state)
		{
			IBStreamer streamer (state, kLittleEndian);
			int32 savedBypass = 0;
			if (!streamer.readInt32 (savedBypass))
				return kResultFalse;
			mBypass = savedBypass > 0;

			return kResultOk;
		}


		tresult PLUGIN_API NapPlugin::getState (IBStream* state)
		{
			IBStreamer streamer (state, kLittleEndian);

			streamer.writeInt32 (mBypass ? 1 : 0);

			return kResultOk;
		}


		tresult PLUGIN_API NapPlugin::setupProcessing (ProcessSetup& newSetup)
		{
			// called before the process call, always in a disable state (not active)
			// here we keep a trace of the processing mode (offline,...) for example.
			mAudioService->getNodeManager().setSampleRate(newSetup.sampleRate);
			mAudioService->getNodeManager().setInternalBufferSize(newSetup.maxSamplesPerBlock);

			mProcessingMode = newSetup.processMode;
			return SingleComponentEffect::setupProcessing (newSetup);
		}


		tresult PLUGIN_API NapPlugin::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
		                                                    SpeakerArrangement* outputs, int32 numOuts)
		{
			auto& nodeManager = mAudioService->getNodeManager();
			nodeManager.setInputChannelCount(numIns);
			nodeManager.setOutputChannelCount(numOuts);

			SingleComponentEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
			return kResultFalse;
		}


		tresult PLUGIN_API NapPlugin::canProcessSampleSize (int32 symbolicSampleSize)
		{
			if (symbolicSampleSize == kSample32)
				return kResultTrue;

			// we support double processing
			if (symbolicSampleSize == kSample64)
				return kResultTrue;

			return kResultFalse;
		}


		IPlugView* PLUGIN_API NapPlugin::createView (const char* name)
		{
			// mControlThread.enqueue([&](){ mRenderWindow->show(); });
			if (mView != nullptr)
				return mView;

			ViewRect rect = ViewRect(0, 0, 400, 300);
			mView = new NapPluginView(*this, rect);
			return mView;
		}


		tresult PLUGIN_API NapPlugin::setEditorState (IBStream* state)
		{
			return kResultTrue;
		}


		tresult PLUGIN_API NapPlugin::getEditorState (IBStream* state)
		{
			return kResultTrue;
		}


		tresult PLUGIN_API NapPlugin::setParamNormalized (ParamID tag, ParamValue value)
		{
			// called from host to update our parameters state
			tresult result = SingleComponentEffect::setParamNormalized (tag, value);
			return result;
		}


		tresult PLUGIN_API NapPlugin::getParamStringByValue (ParamID tag, ParamValue valueNormalized,
		                                                       String128 string)
		{
			return SingleComponentEffect::getParamStringByValue (tag, valueNormalized, string);
		}


		tresult PLUGIN_API NapPlugin::getParamValueByString (ParamID tag, TChar* string,
		                                                       ParamValue& valueNormalized)
		{
			return SingleComponentEffect::getParamValueByString (tag, string, valueNormalized);
		}


		tresult PLUGIN_API NapPlugin::queryInterface (const TUID iid, void** obj)
		{
			return SingleComponentEffect::queryInterface (iid, obj);
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


	} // namespace Vst

} // namespace Steinberg


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
