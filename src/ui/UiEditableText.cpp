#include "UiEditableText.hpp"

UiEditableText::UiEditableText(IniReader * ini_reader, ResourceManager * resource_manager, std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;

  this->id = ini_reader->getInt(name, "id");
  this->layer = ini_reader->getInt(name, "layer", 1);
  this->font = ini_reader->getInt(name, "font");
  if (ini_reader->getInt(name, "anchor", 0) != 0) {
    this->anchors.push_back(ini_reader->getInt(name, "anchor"));
  }
}

UiEditableText::~UiEditableText() {
  if (this->text != nullptr) {
    SDL_DestroyTexture(this->text);
  }
  for (UiElement * child : this->children) {
    delete child;
  }
}

void UiEditableText::setText(const std::string &text_string) {
  if (text_string == this->text_string) {
    return;
  }
  this->text_string = text_string;
  if (this->text != nullptr) {
    SDL_DestroyTexture(this->text);
    this->text = nullptr;
  }
}

void UiEditableText::draw(SDL_Renderer * renderer, SDL_FRect * layout_rect) {
  this->generateDrawRect(this->ini_reader->getSection(this->name), layout_rect);
  if (this->text_string.empty()) {
    return;
  }
  if (this->text == nullptr) {
    std::vector<std::string> color_values = this->ini_reader->getList(this->name, "forecolor");
    SDL_Color color = {255, 255, 255, 255};
    if (color_values.size() == 3) {
      color = {
        (uint8_t) std::stoi(color_values[0]),
        (uint8_t) std::stoi(color_values[1]),
        (uint8_t) std::stoi(color_values[2]),
        255,
      };
    }
    this->text = this->resource_manager->getStringTexture(renderer, this->font, this->text_string, color);
  }
  if (this->text == nullptr) {
    return;
  }
  SDL_FRect text_rect = {this->draw_rect.x, this->draw_rect.y, 0.0f, 0.0f};
  SDL_GetTextureSize(this->text, &text_rect.w, &text_rect.h);
  if (this->draw_rect.h == 0.0f) {
    this->draw_rect.h = text_rect.h;
  }
  SDL_RenderTexture(renderer, this->text, NULL, &text_rect);
  this->drawChildren(renderer, &this->draw_rect);
}
