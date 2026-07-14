#ifndef LOAD_SCREEN_HPP
#define LOAD_SCREEN_HPP

#include "../engine/ResourceManager.hpp"
#include "GameManager.hpp"
#include "../engine/Config.hpp"
#include "../engine/Window.hpp"

class LoadScreen {
public:
  static void run(Window * window, Config * config, ResourceManager * resource_manager, GameManager * game_manager);
};

#endif // LOAD_SCREEN_HPP