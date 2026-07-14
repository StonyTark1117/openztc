#include "UiListBox.hpp"

UiListBox::UiListBox(IniReader * ini_reader, ResourceManager * resource_manager, std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;

  this->id = ini_reader->getInt(name, "id");
  this->layer = ini_reader->getInt(name, "layer", 1);
  this->font = ini_reader->getInt(name, "font");
  this->transparent = ini_reader->getInt(name, "transparent", 0) == 1;
  this->scroll_bar_id = ini_reader->getInt(name, "scrollbar", 0);

  this->fore_color = this->getColor("forecolor", {255, 255, 255, 255});
  this->back_color = this->getColor("backcolor", {0, 0, 0, 255});
  this->highlight_color = this->getColor("highlightcolor", this->fore_color);
  this->highlight_border_color = this->getColor("highlightbordercolor", this->highlight_color);
  this->select_color = this->getColor("selectcolor", this->fore_color);
  this->select_back_color = this->getColor("selectbackcolor", this->back_color);
  this->select_border_color = this->getColor("selectbordercolor", this->select_color);
}

UiListBox::~UiListBox() {
  this->destroyItemTextures();
  for (UiElement * child : this->children) {
    delete child;
  }
}

SDL_Color UiListBox::getColor(const std::string &key, SDL_Color default_color) {
  std::vector<int> color_values = this->ini_reader->getIntList(this->name, key);
  if (color_values.size() != 3) {
    return default_color;
  }
  return {(uint8_t) color_values[0], (uint8_t) color_values[1], (uint8_t) color_values[2], 255};
}

void UiListBox::destroyItemTextures() {
  for (SDL_Texture * texture : this->item_textures) {
    if (texture != nullptr) {
      SDL_DestroyTexture(texture);
    }
  }
  this->item_textures.clear();
  if (this->selected_texture != nullptr) {
    SDL_DestroyTexture(this->selected_texture);
    this->selected_texture = nullptr;
  }
  this->selected_texture_index = -1;
}

void UiListBox::setItems(std::vector<std::string> items) {
  this->items = items;
  this->destroyItemTextures();
  this->selected_index = -1;
  this->highlighted_index = -1;
}

int UiListBox::getSelectedIndex() {
  return this->selected_index;
}

int UiListBox::getScrollBarId() {
  return this->scroll_bar_id;
}

void UiListBox::setScrollBar(UiScrollBar * scroll_bar) {
  this->scroll_bar = scroll_bar;
}

int UiListBox::getVisibleRowCount() {
  if (this->row_height <= 0.0f) {
    return (int) this->items.size();
  }
  return (int) (this->draw_rect.h / this->row_height);
}

int UiListBox::getFirstVisibleRow() {
  if (this->scroll_bar == nullptr) {
    return 0;
  }
  return this->scroll_bar->getValue();
}

UiAction UiListBox::handleInputs(std::vector<Input> &inputs) {
  UiAction result = {Action::NONE, 0, 0};
  for (Input input : inputs) {
    if (input.type != InputType::POSITIONED) {
      continue;
    }
    if (!SDL_PointInRectFloat(&input.position, &this->draw_rect)) {
      if (this->highlighted_index != -1) {
        this->highlighted_index = -1;
      }
      continue;
    }
    if (this->row_height <= 0.0f) {
      continue;
    }
    int row = this->getFirstVisibleRow() + (int) ((input.position.y - this->draw_rect.y) / this->row_height);
    if (row < 0 || row >= (int) this->items.size()) {
      this->highlighted_index = -1;
      continue;
    }
    switch (input.event) {
      case InputEvent::CURSOR_MOVE:
        this->highlighted_index = row;
        break;
      case InputEvent::LEFT_CLICK:
        this->selected_index = row;
        result = {Action::NONE, 0, this->id};
        break;
      default:
        break;
    }
  }
  if (result.source == 0) {
    result = handleInputChildren(inputs);
  }
  return result;
}

void UiListBox::draw(SDL_Renderer * renderer, SDL_FRect * layout_rect) {
  this->generateDrawRect(this->ini_reader->getSection(this->name), layout_rect);

  if (!this->transparent) {
    SDL_SetRenderDrawColor(renderer, this->back_color.r, this->back_color.g, this->back_color.b, 255);
    SDL_RenderFillRect(renderer, &this->draw_rect);
  }

  // Item textures are created lazily because a renderer is needed
  if (this->item_textures.size() != this->items.size()) {
    this->destroyItemTextures();
    this->row_height = 0.0f;
    for (std::string item : this->items) {
      SDL_Texture * texture = this->resource_manager->getStringTexture(renderer, this->font, item, this->fore_color);
      if (texture != nullptr) {
        float width = 0.0f;
        float height = 0.0f;
        SDL_GetTextureSize(texture, &width, &height);
        if (height > this->row_height) {
          this->row_height = height;
        }
      }
      this->item_textures.push_back(texture);
    }
  }

  int first_row = this->getFirstVisibleRow();
  int visible_rows = this->getVisibleRowCount();
  for (int row = first_row; row < first_row + visible_rows && row < (int) this->items.size(); row++) {
    SDL_FRect row_rect = {
      this->draw_rect.x,
      this->draw_rect.y + (float) (row - first_row) * this->row_height,
      this->draw_rect.w,
      this->row_height,
    };

    SDL_Texture * texture = this->item_textures[row];
    if (row == this->selected_index) {
      SDL_SetRenderDrawColor(renderer, this->select_back_color.r, this->select_back_color.g, this->select_back_color.b, 255);
      SDL_RenderFillRect(renderer, &row_rect);
      SDL_SetRenderDrawColor(renderer, this->select_border_color.r, this->select_border_color.g, this->select_border_color.b, 255);
      SDL_RenderRect(renderer, &row_rect);
      if (this->selected_texture_index != row) {
        if (this->selected_texture != nullptr) {
          SDL_DestroyTexture(this->selected_texture);
        }
        this->selected_texture = this->resource_manager->getStringTexture(renderer, this->font, this->items[row], this->select_color);
        this->selected_texture_index = row;
      }
      if (this->selected_texture != nullptr) {
        texture = this->selected_texture;
      }
    } else if (row == this->highlighted_index) {
      SDL_SetRenderDrawColor(renderer, this->highlight_border_color.r, this->highlight_border_color.g, this->highlight_border_color.b, 255);
      SDL_RenderRect(renderer, &row_rect);
    }

    if (texture != nullptr) {
      SDL_FRect text_rect = {row_rect.x + 2.0f, row_rect.y, 0.0f, 0.0f};
      SDL_GetTextureSize(texture, &text_rect.w, &text_rect.h);
      if (text_rect.w > row_rect.w - 4.0f) {
        text_rect.w = row_rect.w - 4.0f;
      }
      SDL_RenderTexture(renderer, texture, NULL, &text_rect);
    }
  }

  if (this->scroll_bar != nullptr) {
    int range = (int) this->items.size() - visible_rows;
    this->scroll_bar->setRange(range);
    if (range > 0) {
      float scroll_bar_width = 16.0f;
      this->scroll_bar->setScrollRect({
        this->draw_rect.x + this->draw_rect.w,
        this->draw_rect.y,
        scroll_bar_width,
        this->draw_rect.h,
      });
    }
  }

  this->drawChildren(renderer, &this->draw_rect);
}
