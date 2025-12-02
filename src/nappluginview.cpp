//
// Created by Stijn on 11/11/2025.
//

#include "nappluginview.h"
#include "napplugin.h"

#import <AppKit/AppKit.h>

#include <vstgui/lib/ccolor.h>

// NAP input events
#include <inputevent.h>

namespace Steinberg
{

	namespace Vst
	{

		// VSTGUI bridge: translate VSTGUI mouse events to NAP
		class NapPluginView::NapInputBridgeView : public VSTGUI::CView
		{
		public:
			NapInputBridgeView(NapPluginView& pluginView)
			: VSTGUI::CView(VSTGUI::CRect(0, 0, pluginView.mWidth, pluginView.mHeight)), mPluginView(pluginView)
			{
				setWantsFocus(true);
				setMouseEnabled(true);
			}

			VSTGUI::CMouseEventResult onMouseDown (VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override
			{
				mWindowID = (int)SDL_GetWindowID(mPluginView.mSDLWindow);
				int px, py;
				toNAP(where, px, py);
				nap::Logger::info("clk %f %f", where.x, where.y);
				auto btn = mapButton(buttons);
				if (btn != nap::PointerClickEvent::EButton::UNKNOWN)
				{
					nap::PointerPressEvent ev(px, py, btn, mWindowID, nap::PointerEvent::ESource::Mouse);
					mPluginView.mPlugin->processNAPInputEvent(ev);
				}
				mLastX = px; mLastY = py;
				return VSTGUI::kMouseEventHandled;
			}

			VSTGUI::CMouseEventResult onMouseUp (VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override
			{
				int px, py; toNAP(where, px, py);
				auto btn = mapButton(buttons);
				if (btn != nap::PointerClickEvent::EButton::UNKNOWN)
				{
					nap::PointerReleaseEvent ev(px, py, btn, mWindowID, nap::PointerEvent::ESource::Mouse);
					mPluginView.mPlugin->processNAPInputEvent(ev);
				}
				mLastX = px; mLastY = py;
				return VSTGUI::kMouseEventHandled;
			}

			VSTGUI::CMouseEventResult onMouseMoved (VSTGUI::CPoint& where, const VSTGUI::CButtonState& buttons) override
			{
				int px, py; toNAP(where, px, py);
				int rx = px - mLastX;
				int ry = py - mLastY;
				nap::Logger::info("mov %f %f", where.x, where.y);
				nap::PointerMoveEvent ev(rx, ry, px, py, mWindowID, nap::PointerEvent::ESource::Mouse);
				mPluginView.mPlugin->processNAPInputEvent(ev);
				mLastX = px; mLastY = py;
				return VSTGUI::kMouseEventHandled;
			}

			// bool onMouseWheelEvent (VSTGUI::MouseWheelEvent& event) override
			// {
			// 	updateWindowID();
			// 	int wx = 0, wy = 0;
			// 	if (axis == VSTGUI::MouseWheelAxis::kMouseWheelAxisX) wx = static_cast<int>(distance);
			// 	else wy = static_cast<int>(distance);
			// 	nap::MouseWheelEvent ev(wx, wy, mWindowID);
			// 	mPlugin.processNAPInputEvent(ev);
			// 	return true;
			// }

			// Optional hover enter/exit could be added if needed.

			void setViewSize (const VSTGUI::CRect& rect)
			{
				CView::setViewSize(rect);
			}

		private:
			NapPluginView& mPluginView;
			int mLastX = 0;
			int mLastY = 0;
			int mWindowID = 0;

			void toNAP (const VSTGUI::CPoint& p, int& outX, int& outY)
			{
				const auto& r = getViewSize();
				outX = static_cast<int>(p.x + mPluginView.mLeft);
				outY = static_cast<int>(r.getHeight() - 1 - p.y + mPluginView.mTop); // flip Y (top-left -> bottom-left)
			}

			static nap::PointerClickEvent::EButton mapButton (const VSTGUI::CButtonState& buttons)
			{
				if (buttons.isLeftButton()) return nap::PointerClickEvent::EButton::LEFT;
				if (buttons.isRightButton()) return nap::PointerClickEvent::EButton::RIGHT;
				if (buttons.isMiddleButton()) return nap::PointerClickEvent::EButton::MIDDLE;
				return nap::PointerClickEvent::EButton::UNKNOWN;
			}
		};


		void NapPluginView::attachedToParent()
		{
			nap::utility::ErrorState errorState;
			SDL_PropertiesID props = SDL_CreateProperties();
			NSView* view = (NSView*)systemWindow;
			mLeft = view.frame.origin.x;
			mTop = view.frame.origin.y;
			mWidth = view.frame.size.width;
			mHeight = view.frame.size.height;

			// Ensure the embedded Cocoa view is prepared for Vulkan on macOS by attaching a CAMetalLayer.
			// SDL requires a Metal-backed view for Vulkan surfaces via MoltenVK.
			bool metalSet = SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN, true);
			if (!errorState.check(metalSet, "Unable to enable '%s', error: %s", SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN, SDL_GetError()))
			{
				nap::Logger::error(errorState.toString().c_str());
				return;
			}

			auto setup = SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_COCOA_VIEW_POINTER, systemWindow);
			if (!errorState.check(setup, "Unable to enable '%s', error: %s", SDL_PROP_WINDOW_CREATE_COCOA_VIEW_POINTER, SDL_GetError()))
			{
				nap::Logger::error(errorState.toString().c_str());
				return;
			}

			// Create SDL window from host NSView
			mSDLWindow = SDL_CreateWindowWithProperties(props);
			if (!errorState.check(mSDLWindow != nullptr, "Failed to create window from handle: %s", SDL_GetError()))
			{
				nap::Logger::error(errorState.toString().c_str());
				return;
			}

			std::atomic<bool> done(false);
			mPlugin->getControlThread().enqueue([&]()
			{
				nap::utility::ErrorState errorState;
				mRenderWindow = std::make_unique<nap::RenderWindow>(mPlugin->getCore(), mSDLWindow);
				if (!mRenderWindow->init(errorState))
				{
					mRenderWindow = nullptr;
					nap::Logger::error(errorState.toString().c_str());
				}
				mRenderWindow->show();
				done = true;
			});
			while (!done)
				mMainThreadQueue.process();

			// Create a VSTGUI frame attached to the host window and add the input bridge on top
			VSTGUI::CRect frameSize (0, 0, mWidth, mHeight);
			mVSTGUIFrame = new VSTGUI::CFrame(frameSize, nullptr);
			if (mVSTGUIFrame->open(systemWindow))
			{
				auto color = VSTGUI::CColor(0, 0, 0, 0);
				mVSTGUIFrame->setBackgroundColor(color);
				mVSTGUIBridge = new NapInputBridgeView(*this);
				mVSTGUIFrame->addView(mVSTGUIBridge);
				mPlugin->setUseVSTGUIInput(true);
			}
			else
			{
				mVSTGUIFrame = nullptr;
			}
		}


		void NapPluginView::removedFromParent()
		{
			// Tear down VSTGUI bridge first
			if (mVSTGUIFrame)
			{
				if (mVSTGUIBridge)
				{
					mVSTGUIFrame->removeView(mVSTGUIBridge);
					mVSTGUIBridge = nullptr;
				}
				mVSTGUIFrame->close();
				mVSTGUIFrame = nullptr;
			}
			if (mPlugin)
				mPlugin->setUseVSTGUIInput(false);

			destroyRenderWindow();
		}


		tresult NapPluginView::onSize(ViewRect *newSize)
		{
			auto result = CPluginView::onSize(newSize);
			if (newSize)
			{
				mWidth = static_cast<float>(newSize->right - newSize->left);
				mHeight = static_cast<float>(newSize->bottom - newSize->top);

				// Resize VSTGUI frame and bridge
				if (mVSTGUIFrame)
				{
					mVSTGUIFrame->setSize(mWidth, mHeight);
					if (mVSTGUIBridge)
					{
						VSTGUI::CRect r(0, 0, mWidth, mHeight);
						mVSTGUIBridge->setViewSize(r);
					}
				}
			}
			return result;
		}


		void NapPluginView::destroyRenderWindow()
		{
			mPlugin->viewClosed();
			if (mRenderWindow != nullptr)
			{
				std::atomic<bool> done(false);
				mPlugin->getControlThread().enqueue([&]()
				{
					mRenderWindow->onDestroy();
					mRenderWindow = nullptr;
					done = true;
				});
				while (!done)
					mMainThreadQueue.process();
			}
		}

	}

} // nap