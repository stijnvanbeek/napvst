//
// Created by Stijn on 11/11/2025.
//

#include "nappluginview.h"
#include "napplugin.h"

namespace Steinberg
{

	namespace Vst
	{


		NapPluginView::~NapPluginView()
		{
		}


		void NapPluginView::attachedToParent()
		{
			nap::utility::ErrorState errorState;
			SDL_PropertiesID props = SDL_CreateProperties();
			// Ensure the embedded Cocoa view is prepared for Vulkan on macOS by attaching a CAMetalLayer.
			// SDL requires a Metal-backed view for Vulkan surfaces via MoltenVK.
			bool mset = SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN, true);
			if (!errorState.check(mset, "Unable to enable '%s', error: %s", SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN, SDL_GetError()))
			{
				nap::Logger::error(errorState.toString().c_str());
				return;
			}

			// bool vset = SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
			// if (!errorState.check(vset, "Unable to enable '%s', error: %s", SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, SDL_GetError()))
			// {
			// 	nap::Logger::error(errorState.toString().c_str());
			// 	return;
			// }

			auto setup = SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_COCOA_VIEW_POINTER, systemWindow);
			auto valid = SDL_HasProperty(props, SDL_PROP_WINDOW_CREATE_COCOA_VIEW_POINTER);
			auto type = SDL_GetPropertyType(props, SDL_PROP_WINDOW_CREATE_COCOA_VIEW_POINTER);
			if (!errorState.check(setup, "Unable to enable '%s', error: %s", SDL_PROP_WINDOW_CREATE_COCOA_VIEW_POINTER, SDL_GetError()))
			{
				nap::Logger::error(errorState.toString().c_str());
				return;
			}

			// Create SDL window from QWindow
			SDL_Window* sdl_window = nullptr;
			try
			{
				sdl_window = SDL_CreateWindowWithProperties(props);
				if (!errorState.check(sdl_window != nullptr, "Failed to create window from handle: %s", SDL_GetError()))
				{
					nap::Logger::error(errorState.toString().c_str());
					return;
				}
			}
			catch (const std::exception& e)
			{
				nap::Logger::error(SDL_GetError());
			}

			mPlugin->getControlThread().enqueue([this, sdl_window]()
			{
				nap::utility::ErrorState errorState;
				mRenderWindow = std::make_unique<nap::RenderWindow>(mPlugin->getCore(), sdl_window);
				if (!mRenderWindow->init(errorState))
				{
					mRenderWindow = nullptr;
					nap::Logger::error(errorState.toString().c_str());
				}
				mRenderWindow->show();
			});
		}


		void NapPluginView::removedFromParent()
		{
			mPlugin->getControlThread().enqueue([this]()
			{
				mRenderWindow->onDestroy();
				mRenderWindow = nullptr;
			}, true);
		}


	}

} // nap