#ifndef UI_SCROLL_BAR_HPP
#define UI_SCROLL_BAR_HPP

#include <SDL3/SDL.h>

#include "UiElement.hpp"

#include "../Animation.hpp"
#include "../IniReader.hpp"
#include "../ResourceManager.hpp"
#include "../Input.hpp"

class UiScrollBar : public UiElement {
public:
  UiScrollBar(IniReader * ini_reader, ResourceManager * resource_manager, std::string name);
  ~UiScrollBar();

  UiAction handleInputs(std::vector<Input> &inputs);
  void draw(SDL_Renderer * renderer, SDL_FRect * layout_rect);

  // Called by the element this scroll bar scrolls, since the layout data
  // does not give scroll bars a position of their own
  void setScrollRect(SDL_FRect rect);
  void setRange(int range);
  int getValue();
  void setValue(int value);

private:
  Animation * background = nullptr;
  Animation * up_arrow = nullptr;
  Animation * down_arrow = nullptr;
  Animation * thumb = nullptr;

  int value = 0;
  int range = 0;
  bool has_scroll_rect = false;
  float arrow_height = 0.0f;

  SDL_FRect getUpArrowRect();
  SDL_FRect getDownArrowRect();
  SDL_FRect getThumbRect();
};

#endif // UI_SCROLL_BAR_HPP
