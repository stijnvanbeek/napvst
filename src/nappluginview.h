#pragma once

#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/common/pluginview.h"

#include <renderwindow.h>

namespace Steinberg
{

	class NapPluginView : public CPluginView
	{
	public:
		NapPluginView(const ViewRect* rect) : CPluginView(rect) { }
		NapPluginView () = default;
		~NapPluginView () = default;

		tresult PLUGIN_API isPlatformTypeSupported (FIDString type) override { return kResultTrue; }
		void attachedToParent () override {}
		void removedFromParent () override {}

	private:
	};

} // Steinberg
