#ifndef UI_RADIO_SET_HPP
#define UI_RADIO_SET_HPP

#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "UiElement.hpp"
#include "UiButton.hpp"
#include "../engine/IniReader.hpp"
#include "../engine/ResourceManager.hpp"

// A group of buttons of which one is selected at a time, like the tab rows
// on the in-game panels. The layout links the member buttons by id after all
// sections are processed. Radio sets with a template button and scroll bar,
// like the icon grids, get their content from game data and are not
// supported yet.
class UiRadioSet : public UiElement {
public:
  UiRadioSet(IniReader * ini_reader, ResourceManager * resource_manager, std::string name);
  ~UiRadioSet();

  void draw(SDL_Renderer * renderer, SDL_FRect * layout_rect);

  std::vector<int> getButtonIds();
  void addButton(UiButton * button);
  void select(int button_id);
  bool hasButton(int button_id);

private:
  std::vector<int> button_ids;
  std::vector<UiButton *> buttons;
};

#endif // UI_RADIO_SET_HPP
