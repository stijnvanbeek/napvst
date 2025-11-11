#pragma once

#include "pluginterfaces/gui/iplugview.h"
#include "vstgui4/vstgui/lib/events.h"
#include "vstgui4/vstgui/lib/platform/iplatformframecallback.h"

#include <renderwindow.h>

#include "vstgui4/vstgui/lib/platform/linux/x11timer.h"

namespace Steinberg
{

	class NapPluginView : public IPlugView
	{
	public:
		NapPluginView (nap::RenderWindow& window) : mWindow(window) {}
		~NapPluginView () = default;
		tresult isPlatformTypeSupported (FIDString type) override { return kResultTrue; }
		tresult attached(void* parent, FIDString type) override { }
		tresult removed() override { return kResultTrue; }
		tresult onWheel(float distance) override { return kResultTrue; }
		tresult PLUGIN_API onKeyDown (char16 key, int16 keyCode, int16 modifiers) override { return kResultTrue; }
		tresult PLUGIN_API onKeyUp (char16 key, int16 keyCode, int16 modifiers) override { return kResultTrue; }
		tresult PLUGIN_API getSize (ViewRect* size) override { return kResultTrue; }
		tresult PLUGIN_API onSize (ViewRect* newSize) override { return kResultTrue; }
		tresult PLUGIN_API onFocus (TBool state) override { return kResultTrue; }
		tresult PLUGIN_API setFrame (IPlugFrame* frame) override { return kResultTrue; }
		tresult PLUGIN_API canResize () override { return kResultTrue; }
		tresult PLUGIN_API checkSizeConstraint (ViewRect* rect) override { return kResultTrue; }

		//---Interface---------
		tresult PLUGIN_API queryInterface (const TUID iid, void** obj) SMTG_OVERRIDE { return kResultOk; }
		uint32 PLUGIN_API addRef () override { return ++mRefCounter; }
		uint32 PLUGIN_API release () override { return --mRefCounter; }


	private:
		nap::RenderWindow& mWindow;
		static uint32 mRefCounter;
	};

} // Steinberg
