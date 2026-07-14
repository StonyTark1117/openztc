#include "UiText.hpp"

UiText::UiText(IniReader * ini_reader, ResourceManager * resource_manager, std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;

  this->id = ini_reader->getInt(name, "id");
  this->layer = ini_reader->getInt(name, "layer", 1);
  if (ini_reader->getInt(name, "anchor", 0) != 0) {
    this->anchors.push_back(ini_reader->getInt(name, "anchor"));
  }
  if (ini_reader->getInt(name, "anchor1", 0) != 0) {
    this->anchors.push_back(ini_reader->getInt(name, "anchor1"));
  }
  if (ini_reader->getInt(name, "anchor2", 0) != 0) {
    this->anchors.push_back(ini_reader->getInt(name, "anchor2"));
  }
  this->font = ini_reader->getInt(name, "font");
  this->scroll_bar_id = ini_reader->getInt(name, "scrollbar", 0);
  if (this->scroll_bar_id != 0) {
    // Scrollable text boxes get their content at runtime
    return;
  }
  uint32_t string_id = (uint32_t) ini_reader->getUnsignedInt(name, "id");
  this->text_string = this->resource_manager->getString(string_id);

  if(this->text_string.empty()) {
    if (this->id == 7119) {
      this->text_string = "Version Number: OpenZTC 0.1  ";
    } else {
      this->text_string = "Not found";
    }
  }
}

void UiText::setText(const std::string &text_string) {
  if (text_string == this->text_string) {
    return;
  }
  this->text_string = text_string;
  if (this->text) {
    SDL_DestroyTexture(this->text);
    this->text = nullptr;
  }
  if (this->shadow) {
    SDL_DestroyTexture(this->shadow);
    this->shadow = nullptr;
  }
  if (this->scroll_bar != nullptr) {
    this->scroll_bar->setValue(0);
  }
}

int UiText::getScrollBarId() {
  return this->scroll_bar_id;
}

void UiText::setScrollBar(UiScrollBar * scroll_bar) {
  this->scroll_bar = scroll_bar;
}

void UiText::drawWrapped(SDL_Renderer * renderer, SDL_FRect * layout_rect) {
  this->generateDrawRect(this->ini_reader->getSection(this->name), layout_rect);
  if (this->text_string.empty()) {
    return;
  }
  if (!this->text) {
    std::vector<std::string> color_values = ini_reader->getList(name, "forecolor");
    SDL_Color color = {255, 255, 255, 255};
    if (color_values.size() == 3) {
      color = {
        (uint8_t) std::stoi(color_values[0]),
        (uint8_t) std::stoi(color_values[1]),
        (uint8_t) std::stoi(color_values[2]),
        255,
      };
    }
    this->text = this->resource_manager->getWrappedStringTexture(renderer, this->font, this->text_string, color, (int) this->draw_rect.w);
  }
  if (!this->text) {
    return;
  }

  float text_width = 0.0f;
  float text_height = 0.0f;
  SDL_GetTextureSize(this->text, &text_width, &text_height);
  float line_height = (float) this->resource_manager->getFontLineHeight(this->font);

  int scroll_value = 0;
  if (this->scroll_bar != nullptr) {
    int range = 0;
    if (text_height > this->draw_rect.h && line_height > 0.0f) {
      range = (int) ((text_height - this->draw_rect.h) / line_height) + 1;
    }
    this->scroll_bar->setRange(range);
    if (range > 0) {
      this->scroll_bar->setScrollRect({
        this->draw_rect.x + this->draw_rect.w,
        this->draw_rect.y,
        16.0f,
        this->draw_rect.h,
      });
      scroll_value = this->scroll_bar->getValue();
    }
  }

  float offset = (float) scroll_value * line_height;
  if (offset > text_height - this->draw_rect.h) {
    offset = text_height - this->draw_rect.h;
  }
  if (offset < 0.0f) {
    offset = 0.0f;
  }
  float visible_height = text_height - offset;
  if (visible_height > this->draw_rect.h) {
    visible_height = this->draw_rect.h;
  }
  SDL_FRect source_rect = {0.0f, offset, text_width, visible_height};
  SDL_FRect destination_rect = {this->draw_rect.x, this->draw_rect.y, text_width, visible_height};
  SDL_RenderTexture(renderer, this->text, &source_rect, &destination_rect);
  this->drawChildren(renderer, &draw_rect);
}

UiText::~UiText() {
  SDL_DestroyTexture(text);
  for (UiElement * child : this->children) {
    delete child;
  }
}

void UiText::draw(SDL_Renderer * renderer, SDL_FRect * layout_rect) {
  if (this->scroll_bar_id != 0) {
    this->drawWrapped(renderer, layout_rect);
    return;
  }
  if (!this->text_string.empty() && (!this->text || !this->shadow)) {
    std::vector<std::string> color_values = ini_reader->getList(name, "forecolor");
    SDL_Color color = {
      (uint8_t) std::stoi(color_values[0]),
      (uint8_t) std::stoi(color_values[1]),
      (uint8_t) std::stoi(color_values[2]),
      255,
    };
    this->text = this->resource_manager->getStringTexture(renderer, this->font, this->text_string, color);
    this->shadow = this->resource_manager->getStringTexture(renderer, this->font, this->text_string,  {0, 0, 0, 255});
  }
  this->generateDrawRect(this->ini_reader->getSection(this->name), layout_rect);
  SDL_FRect text_rect = {draw_rect.x, draw_rect.y, 0.0f, 0.0f};
  SDL_GetTextureSize(this->text, &text_rect.w, &text_rect.h);
  if (draw_rect.w == 0) {
    this->draw_rect.w = text_rect.w;
  }
  if (draw_rect.h == 0) {
    this->draw_rect.h = text_rect.h;
    if (this->ini_reader->get(this->name, "y") == "bottom") {
      text_rect.y -= text_rect.h;
    }
  }
  if (this->ini_reader->get(this->name, "justify") == "center") {
    text_rect.x = draw_rect.x + (draw_rect.w / 2.0f) - (text_rect.w / 2.0f);
    text_rect.y = draw_rect.y + (draw_rect.h / 2.0f) - (text_rect.h / 2.0f);
  } else if (this->ini_reader->get(this->name, "justify") == "right") {
    text_rect.x = draw_rect.x + draw_rect.w - text_rect.w;
  }
  
  shadow_rect = {text_rect.x - 1.0f, text_rect.y + 1.0f, text_rect.w, text_rect.h};
  SDL_RenderTexture(renderer, this->shadow, NULL, &shadow_rect);

  SDL_RenderTexture(renderer, this->text, NULL, &text_rect);
  this->drawChildren(renderer, &draw_rect);
}
