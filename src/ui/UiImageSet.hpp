#ifndef UI_IMAGE_SET_HPP
#define UI_IMAGE_SET_HPP

#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "UiElement.hpp"
#include "../engine/IniReader.hpp"
#include "../engine/ResourceManager.hpp"

// A set of images of which one is shown at a time, like the male/female icon
// on the animal panel. Which one is picked with setIndex at runtime.
class UiImageSet : public UiElement {
public:
  UiImageSet(IniReader * ini_reader, ResourceManager * resource_manager, std::string name);
  ~UiImageSet();

  void draw(SDL_Renderer * renderer, SDL_FRect * layout_rect);

  void setIndex(int index);

protected:
  std::vector<std::string> image_paths;
  std::vector<SDL_Texture *> images;
  int index = 0;
};

#endif // UI_IMAGE_SET_HPP
