#ifndef UI_EDITABLE_TEXT_HPP
#define UI_EDITABLE_TEXT_HPP

#include <string>

#include <SDL3/SDL.h>

#include "UiElement.hpp"
#include "../engine/IniReader.hpp"
#include "../engine/ResourceManager.hpp"

// A text field the application can change at runtime. Editing with the
// keyboard is not supported yet, since key events are not handled anywhere.
class UiEditableText : public UiElement {
public:
  UiEditableText(IniReader * ini_reader, ResourceManager * resource_manager, std::string name);
  ~UiEditableText();

  void draw(SDL_Renderer * renderer, SDL_FRect * layout_rect);

  void setText(const std::string &text_string);

private:
  std::string text_string = "";
  SDL_Texture * text = nullptr;
  int font = 0;
};

#endif // UI_EDITABLE_TEXT_HPP
