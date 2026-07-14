// Developer tool: renders a zoo map to a bmp file without needing a
// display, using the offscreen video driver
#include <atomic>
#include <cstdio>

#include <SDL3/SDL.h>

#include "../src/engine/Config.hpp"
#include "../src/engine/ResourceManager.hpp"
#include "../src/game/MapView.hpp"

int main(int argc, char ** argv) {
  if (argc < 3) {
    printf("usage: mapshot <map name, like maps/airport.zoo> <output.bmp> [width] [height] [zoom]\n");
    return 1;
  }
  int width = argc > 3 ? atoi(argv[3]) : 800;
  int height = argc > 4 ? atoi(argv[4]) : 600;

  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    printf("SDL init failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window * window = SDL_CreateWindow("mapshot", width, height, 0);
  SDL_Renderer * renderer = SDL_CreateRenderer(window, NULL);
  if (renderer == NULL) {
    printf("No renderer: %s\n", SDL_GetError());
    return 1;
  }

  Config config;
  ResourceManager resource_manager(&config);
  std::atomic<float> progress = 0.0f;
  std::atomic<bool> done = false;
  resource_manager.load_resource_map(&progress, &done);

  MapView view(&resource_manager);
  if (!view.loadMap(argv[1])) {
    printf("Could not load %s\n", argv[1]);
    return 1;
  }
  if (argc > 5) {
    view.setZoom((float) atof(argv[5]));
  }

  SDL_FRect window_rect = {0.0f, 0.0f, (float) width, (float) height};
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  view.draw(renderer, &window_rect);

  SDL_Surface * surface = SDL_RenderReadPixels(renderer, NULL);
  if (surface == NULL) {
    printf("Could not read pixels: %s\n", SDL_GetError());
    return 1;
  }
  if (!SDL_SaveBMP(surface, argv[2])) {
    printf("Could not save %s: %s\n", argv[2], SDL_GetError());
    return 1;
  }
  printf("Saved %s (%dx%d)\n", argv[2], width, height);
  SDL_DestroySurface(surface);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
