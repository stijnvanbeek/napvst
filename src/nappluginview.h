#pragma once

#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/common/pluginview.h"

#include <renderwindow.h>

namespace Steinberg
{

	namespace Vst
	{

		class NapPlugin;


		class NapPluginView : public CPluginView
		{
		public:
			NapPluginView(NapPlugin& plugin, ViewRect& rect) : mPlugin(&plugin), CPluginView(&rect) { }
			~NapPluginView ();

			tresult PLUGIN_API isPlatformTypeSupported (FIDString type) override { return kResultTrue; }
			void attachedToParent () override;
			void removedFromParent () override;

			nap::RenderWindow* getRenderWindow () { return mRenderWindow.get(); }
			bool isAttached() const { return systemWindow != nullptr && mRenderWindow != nullptr; }

		private:
			std::unique_ptr<nap::RenderWindow> mRenderWindow = nullptr;
			NapPlugin* mPlugin = nullptr;
		};

	}

} // Steinberg
