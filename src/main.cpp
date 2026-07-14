#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Config.hpp"
#include "Window.hpp"
#include "ResourceManager.hpp"
#include "CursorManager.hpp"
#include "LoadScreen.hpp"
#include "InputManager.hpp"
#include "Input.hpp"
#include "GameManager.hpp"


int main(int argc, char **argv) {
  (void) argc;
  (void) argv;

  Config config;
  ResourceManager resource_manager(&config);
  InputManager input_manager;
  std::vector<Input> inputs;

  Window window("OpenZTC", config.getScreenWidth(), config.getScreenHeight(), 60.0f);
  CursorManager cursor_manager(&resource_manager);
  cursor_manager.setCursor(CursorRole::DEFAULT);
  GameManager game_manager(&resource_manager, &cursor_manager);

  LoadScreen::run(&window, &config, &resource_manager, &game_manager);

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
      game_manager.Draw(window.renderer, window.getWindowRect());

      window.present();
    }
  }

  return 0;
}