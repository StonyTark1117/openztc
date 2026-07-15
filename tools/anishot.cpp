// Developer tool: renders one animation (optionally a specific direction
// key) to a bmp file, using the offscreen video driver
#include <atomic>
#include <cstdio>

#include <SDL3/SDL.h>

#include "../src/engine/Animation.hpp"
#include "../src/engine/Config.hpp"
#include "../src/engine/ResourceManager.hpp"

int main(int argc, char ** argv) {
  if (argc < 3) {
    printf("usage: anishot <animation path, like objects/fgate2/idle/idle> <output.bmp> [key like SE] [scale]\n");
    return 1;
  }
  const char * key = argc > 3 ? argv[3] : "";
  float scale = argc > 4 ? (float) atof(argv[4]) : 1.0f;

  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    printf("SDL init failed: %s\n", SDL_GetError());
    return 1;
  }

  Config config;
  ResourceManager resource_manager(&config);
  std::atomic<float> progress = 0.0f;
  std::atomic<bool> done = false;
  resource_manager.load_resource_map(&progress, &done);

  Animation * animation = resource_manager.getAnimation(argv[1]);
  if (animation == nullptr) {
    printf("No animation at %s\n", argv[1]);
    return 1;
  }
  float width = 0.0f;
  float height = 0.0f;
  bool have_size = key[0] != '\0' ? animation->getSizeByKey(key, &width, &height)
                                  : animation->getSize(&width, &height);
  if (!have_size) {
    printf("No size for key '%s'\n", key);
    return 1;
  }
  int out_w = (int) (width * scale) + 2;
  int out_h = (int) (height * scale) + 2;
  SDL_Window * window = SDL_CreateWindow("anishot", out_w, out_h, 0);
  SDL_Renderer * renderer = SDL_CreateRenderer(window, NULL);
  if (renderer == NULL) {
    printf("No renderer: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
  SDL_RenderClear(renderer);
  SDL_FRect destination = {1.0f, 1.0f, width * scale, height * scale};
  if (key[0] != '\0') {
    animation->drawByKey(renderer, &destination, key);
  } else {
    animation->draw(renderer, &destination);
  }
  SDL_Surface * surface = SDL_RenderReadPixels(renderer, NULL);
  if (surface == NULL) {
    printf("Could not read pixels: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SaveBMP(surface, argv[2]);
  printf("Saved %s (%dx%d)\n", argv[2], out_w, out_h);
  return 0;
}
