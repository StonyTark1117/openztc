#ifndef UI_BUTTON_HPP
#define UI_BUTTON_HPP

#include <string>

#include <SDL3/SDL.h>

#include "UiElement.hpp"
#include "../engine/IniReader.hpp"
#include "../engine/Animation.hpp"
#include "../engine/ResourceManager.hpp"
#include "../engine/CursorManager.hpp"
#include "../engine/CompassDirection.hpp"

class UiButton : public UiElement {
public:
  UiButton(IniReader * ini_reader, ResourceManager * resource_manager, CursorManager * cursor_manager, std::string name);
  ~UiButton();

  UiAction handleInputs(std::vector<Input> &inputs);
  void draw(SDL_Renderer * renderer, SDL_FRect * layout_rect);

  void setRadioSelected(bool radio_selected);
  // Replaces the label from the layout's textid, the texture regenerates
  // on the next draw
  void setText(const std::string &text);

private:
  void setCursor(CursorRole role);

  std::string text_string = "";
  SDL_Texture * text = nullptr;
  SDL_Texture * shadow = nullptr;
  int font = 0;
  Animation * animation = nullptr;
  bool selected = false;
  bool radio_selected = false;
  bool selected_updated = false;
  bool has_select_color = false;
  CompassDirection current_button_image = CompassDirection::N;
  SDL_FRect shadow_rect = {0.0f, 0.0f, 0.0f, 0.0f};
};

#endif // UI_BUTTON_HPP