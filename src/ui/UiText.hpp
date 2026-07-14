#ifndef UI_TEXT_HPP
#define UI_TEXT_HPP

#include <string>

#include <SDL3/SDL.h>

#include "UiElement.hpp"
#include "UiScrollBar.hpp"
#include "../IniReader.hpp"
#include "../ResourceManager.hpp"

class UiText : public UiElement {
public:
  UiText(IniReader * ini_reader, ResourceManager * resource_manager, std::string name);
  ~UiText();

  void draw(SDL_Renderer * renderer, SDL_FRect * layout_rect);

  void setText(const std::string &text_string);
  int getScrollBarId();
  void setScrollBar(UiScrollBar * scroll_bar);

private:
  std::string text_string = "";
  SDL_Texture * text = nullptr;
  SDL_Texture * shadow = nullptr;
  int font = 0;
  SDL_FRect shadow_rect = {0.0f, 0.0f, 0.0f, 0.0f};
  int scroll_bar_id = 0;
  UiScrollBar * scroll_bar = nullptr;

  void drawWrapped(SDL_Renderer * renderer, SDL_FRect * layout_rect);
};

#endif // UI_TEXT_HPP
