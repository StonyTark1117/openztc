#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "engine/Config.hpp"
#include "engine/ModManager.hpp"
#include "engine/Window.hpp"
#include "engine/ResourceManager.hpp"
#include "engine/Utils.hpp"
#include "engine/ZtdFile.hpp"
#include "engine/CursorManager.hpp"
#include "game/LoadScreen.hpp"
#include "engine/InputManager.hpp"
#include "engine/Input.hpp"
#include "game/GameManager.hpp"
#include "game/Simulation.hpp"


int main(int argc, char **argv) {
  (void) argc;
  (void) argv;

  Config config;
  ModManager mod_manager(Utils::getZooTycoonPath() + "/mods");
  mod_manager.setArchiveLister(ZtdFile::getFileList);
  mod_manager.load();
  ResourceManager resource_manager(&config);
  resource_manager.setModArchives(mod_manager.getEnabledArchives());
  InputManager input_manager;
  std::vector<Input> inputs;

  Window window("OpenZTC", config.getScreenWidth(), config.getScreenHeight(), 60.0f);
  if (config.getKeepDisplayAwake()) {
    // Long sessions should not fight the screen locker. keepDisplayAwake=0
    // in an openztc section of zoo.ini turns this off.
    SDL_DisableScreenSaver();
  }
  CursorManager cursor_manager(&resource_manager);
  cursor_manager.setCursor(CursorRole::DEFAULT);
  GameManager game_manager(&resource_manager, &cursor_manager, &mod_manager);

  LoadScreen::run(&window, &config, &resource_manager, &game_manager);

  // The renderer runs as fast as the frame limiter allows, the simulation
  // advances in fixed ticks through an accumulator
  const uint64_t tick_duration_ms = 1000 / Simulation::TICKS_PER_SECOND;
  uint64_t previous_time = SDL_GetTicks();
  uint64_t tick_accumulator = 0;

  int running = 1;
  while (running > 0) {
    window.clear();
    inputs = input_manager.getInputs();
    for (Input input : inputs) {
      if (input.event == InputEvent::QUIT) {
        running = 0;
      }
    }
    if (running) {
      running = game_manager.HandleInputs(inputs);

      uint64_t current_time = SDL_GetTicks();
      tick_accumulator += current_time - previous_time;
      previous_time = current_time;
      // Avoid a tick avalanche after a stall, like the window being dragged
      if (tick_accumulator > 1000) {
        tick_accumulator = 1000;
      }
      while (tick_accumulator >= tick_duration_ms) {
        game_manager.TickSimulation();
        tick_accumulator -= tick_duration_ms;
      }

      game_manager.Draw(window.renderer, window.getWindowRect());

      window.present();
    }
  }

  return 0;
}