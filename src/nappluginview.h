#pragma once

#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/common/pluginview.h"

// VSTGUI
#include <vstgui/lib/cframe.h>

#include <renderwindow.h>
#include <utility/threading.h>

namespace Steinberg
{

	namespace Vst
	{

		class NapPlugin;


		class NapPluginView : public CPluginView
		{
		public:
			NapPluginView(NapPlugin& plugin, nap::TaskQueue& mainThreadQueue, ViewRect& rect) : mPlugin(&plugin), mMainThreadQueue(mainThreadQueue), CPluginView(&rect) { }
			~NapPluginView () = default;

			tresult PLUGIN_API isPlatformTypeSupported (FIDString type) override { return kResultTrue; }
			void attachedToParent () override;
			void removedFromParent () override;
			tresult PLUGIN_API onSize (ViewRect* newSize) override;

			nap::RenderWindow* getRenderWindow () { return mRenderWindow.get(); }
			bool isAttached() const { return systemWindow != nullptr && mRenderWindow != nullptr; }

			SDL_Window* getSDLWindowHandle() const { return mSDLWindow; }

		private:
			void destroyRenderWindow();

			std::unique_ptr<nap::RenderWindow> mRenderWindow = nullptr;
			SDL_Window* mSDLWindow = nullptr;
			NapPlugin* mPlugin = nullptr;
			nap::TaskQueue& mMainThreadQueue;
			float mLeft = 0.f;
			float mTop = 0.f;
			float mWidth = 0.f;
			float mHeight = 0.f;

			// VSTGUI input bridge
			class NapInputBridgeView; // forward decl (defined in cpp)
			VSTGUI::CFrame* mVSTGUIFrame = nullptr;
			NapInputBridgeView* mVSTGUIBridge = nullptr;
		};

	}

} // Steinberg
