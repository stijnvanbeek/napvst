#include "sdlpoller.h"

namespace nap
{

	std::unique_ptr<SDLPoller> SDLPoller::mInstance = nullptr;


	SDLPoller::Client::Client()
	{
		SDLPoller::getInstance().mClients.insert(this);
	}


	SDLPoller::Client::~Client()
	{
		SDLPoller::getInstance().mClients.erase(this);
	}


	bool SDLPoller::Client::poll(SDL_Event *aEvent)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			SDLPoller::getInstance().add(event);
		}

		if (!mEvents.empty())
		{
			*aEvent = mEvents.front();
			mEvents.pop();
			return true;
		}
		return false;
	}


	void SDLPoller::Client::add(const SDL_Event &event)
	{
		mEvents.push(event);
	}


	SDLPoller & SDLPoller::getInstance()
	{
		if (mInstance == nullptr)
			mInstance = std::make_unique<SDLPoller>();
		return *mInstance;
	}


	void nap::SDLPoller::add(const SDL_Event &event)
	{
		for (auto& client : mClients)
			client->add(event);
	}

}
