#include "UiImageSet.hpp"

UiImageSet::UiImageSet(IniReader * ini_reader, ResourceManager * resource_manager, std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;

  this->id = ini_reader->getInt(name, "id");
  this->layer = ini_reader->getInt(name, "layer", 1);
  if (ini_reader->getInt(name, "anchor", 0) != 0) {
    this->anchors.push_back(ini_reader->getInt(name, "anchor"));
  }
  this->image_paths = ini_reader->getList(name, "image");
}

UiImageSet::~UiImageSet() {
  for (SDL_Texture * image : this->images) {
    if (image != nullptr) {
      SDL_DestroyTexture(image);
    }
  }
  for (UiElement * child : this->children) {
    delete child;
  }
}

void UiImageSet::setIndex(int index) {
  if (index >= 0 && index < (int) this->image_paths.size()) {
    this->index = index;
  }
}

void UiImageSet::draw(SDL_Renderer * renderer, SDL_FRect * layout_rect) {
  // The textures are created lazily because a renderer is needed
  if (this->images.size() != this->image_paths.size()) {
    for (std::string image_path : this->image_paths) {
      this->images.push_back(this->resource_manager->getTexture(renderer, image_path));
    }
  }

  this->generateDrawRect(this->ini_reader->getSection(this->name), layout_rect);
  if (this->index < 0 || this->index >= (int) this->images.size() || this->images[this->index] == nullptr) {
    return;
  }
  SDL_Texture * image = this->images[this->index];
  if (draw_rect.w == 0.0f || draw_rect.h == 0.0f) {
    SDL_GetTextureSize(image, &draw_rect.w, &draw_rect.h);
  }
  SDL_RenderTexture(renderer, image, NULL, &draw_rect);
  this->drawChildren(renderer, &draw_rect);
}
