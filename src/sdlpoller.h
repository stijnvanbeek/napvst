#pragma once


#include <SDL_events.h>
#include <queue>
#include <set>


namespace nap
{

	class SDLPoller
	{
	public:
		class Client
		{
			friend class SDLPoller;
		public:
			Client();
			~Client();

			bool poll(SDL_Event* aEvent);

		private:
			void add(const SDL_Event& event);
			std::queue<SDL_Event> mEvents;
		};

		SDLPoller() = default;
		~SDLPoller() = default;

		static SDLPoller& getInstance();

	private:
		void add(const SDL_Event& event);

		static std::unique_ptr<SDLPoller> mInstance;
		std::set<Client*> mClients;
	};

}