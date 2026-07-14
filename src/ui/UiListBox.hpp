#ifndef UI_LIST_BOX_HPP
#define UI_LIST_BOX_HPP

#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "UiElement.hpp"
#include "UiScrollBar.hpp"

#include "../engine/IniReader.hpp"
#include "../engine/ResourceManager.hpp"
#include "../engine/Input.hpp"

class UiListBox : public UiElement {
public:
  UiListBox(IniReader * ini_reader, ResourceManager * resource_manager, std::string name);
  ~UiListBox();

  UiAction handleInputs(std::vector<Input> &inputs);
  void draw(SDL_Renderer * renderer, SDL_FRect * layout_rect);

  void setItems(std::vector<std::string> items);
  int getSelectedIndex();
  void setSelectedIndex(int index);
  int getScrollBarId();
  void setScrollBar(UiScrollBar * scroll_bar);

private:
  int font = 0;
  bool transparent = false;
  SDL_Color fore_color = {255, 255, 255, 255};
  SDL_Color back_color = {0, 0, 0, 255};
  SDL_Color highlight_color = {255, 255, 255, 255};
  SDL_Color highlight_border_color = {255, 255, 255, 255};
  SDL_Color select_color = {255, 255, 255, 255};
  SDL_Color select_back_color = {0, 0, 0, 255};
  SDL_Color select_border_color = {255, 255, 255, 255};

  std::vector<std::string> items;
  std::vector<SDL_Texture *> item_textures;
  SDL_Texture * selected_texture = nullptr;
  int selected_texture_index = -1;
  int selected_index = -1;
  int highlighted_index = -1;
  float row_height = 0.0f;
  int scroll_bar_id = 0;
  UiScrollBar * scroll_bar = nullptr;

  SDL_Color getColor(const std::string &key, SDL_Color default_color);
  int getVisibleRowCount();
  int getFirstVisibleRow();
  void destroyItemTextures();
};

#endif // UI_LIST_BOX_HPP
