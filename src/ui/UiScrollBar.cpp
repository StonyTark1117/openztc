#include "UiScrollBar.hpp"

UiScrollBar::UiScrollBar(IniReader * ini_reader, ResourceManager * resource_manager, std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;

  this->id = ini_reader->getInt(name, "id");
  this->layer = ini_reader->getInt(name, "layer", 1);

  std::string background_path = ini_reader->get(name, "animation");
  if (!background_path.empty()) {
    this->background = resource_manager->getAnimation(background_path);
  }
  std::string up_arrow_path = ini_reader->get(name, "uparrow");
  if (!up_arrow_path.empty()) {
    this->up_arrow = resource_manager->getAnimation(up_arrow_path);
  }
  std::string down_arrow_path = ini_reader->get(name, "downarrow");
  if (!down_arrow_path.empty()) {
    this->down_arrow = resource_manager->getAnimation(down_arrow_path);
  }
  std::string thumb_path = ini_reader->get(name, "thumb");
  if (!thumb_path.empty()) {
    this->thumb = resource_manager->getAnimation(thumb_path);
  }

  if (this->up_arrow != nullptr) {
    float arrow_width = 0.0f;
    this->up_arrow->getSize(&arrow_width, &this->arrow_height);
  }
}

UiScrollBar::~UiScrollBar() {
  if (this->background != nullptr) {
    delete this->background;
  }
  if (this->up_arrow != nullptr) {
    delete this->up_arrow;
  }
  if (this->down_arrow != nullptr) {
    delete this->down_arrow;
  }
  if (this->thumb != nullptr) {
    delete this->thumb;
  }
  for (UiElement * child : this->children) {
    delete child;
  }
}

void UiScrollBar::setScrollRect(SDL_FRect rect) {
  this->draw_rect = rect;
  this->has_scroll_rect = true;
}

void UiScrollBar::setRange(int range) {
  if (range < 0) {
    range = 0;
  }
  this->range = range;
  if (this->value > range) {
    this->value = range;
  }
}

int UiScrollBar::getValue() {
  return this->value;
}

void UiScrollBar::setValue(int value) {
  if (value < 0) {
    value = 0;
  }
  if (value > this->range) {
    value = this->range;
  }
  this->value = value;
}

SDL_FRect UiScrollBar::getUpArrowRect() {
  return {this->draw_rect.x, this->draw_rect.y, this->draw_rect.w, this->arrow_height};
}

SDL_FRect UiScrollBar::getDownArrowRect() {
  return {this->draw_rect.x, this->draw_rect.y + this->draw_rect.h - this->arrow_height, this->draw_rect.w, this->arrow_height};
}

SDL_FRect UiScrollBar::getThumbRect() {
  float track_top = this->draw_rect.y + this->arrow_height;
  float track_height = this->draw_rect.h - 2.0f * this->arrow_height;
  float thumb_height = track_height / (float) (this->range + 1);
  if (this->thumb != nullptr) {
    float thumb_width = 0.0f;
    float minimum_height = 0.0f;
    if (this->thumb->getSize(&thumb_width, &minimum_height) && thumb_height < minimum_height) {
      thumb_height = minimum_height;
    }
  }
  float position = 0.0f;
  if (this->range > 0) {
    position = (float) this->value / (float) this->range;
  }
  return {this->draw_rect.x, track_top + position * (track_height - thumb_height), this->draw_rect.w, thumb_height};
}

UiAction UiScrollBar::handleInputs(std::vector<Input> &inputs) {
  UiAction result = {Action::NONE, 0, 0};
  if (!this->has_scroll_rect) {
    return result;
  }
  for (Input input : inputs) {
    if (input.type != InputType::POSITIONED || input.event != InputEvent::LEFT_CLICK) {
      continue;
    }
    if (!SDL_PointInRectFloat(&input.position, &this->draw_rect)) {
      continue;
    }
    SDL_FRect up_arrow_rect = this->getUpArrowRect();
    SDL_FRect down_arrow_rect = this->getDownArrowRect();
    SDL_FRect thumb_rect = this->getThumbRect();
    if (SDL_PointInRectFloat(&input.position, &up_arrow_rect)) {
      if (this->value > 0) {
        this->value--;
      }
    } else if (SDL_PointInRectFloat(&input.position, &down_arrow_rect)) {
      if (this->value < this->range) {
        this->value++;
      }
    } else if (input.position.y < thumb_rect.y) {
      // Clicking the track above or below the thumb scrolls a full step
      if (this->value > 0) {
        this->value--;
      }
    } else if (input.position.y > thumb_rect.y + thumb_rect.h) {
      if (this->value < this->range) {
        this->value++;
      }
    }
    result = {Action::NONE, 0, this->id};
  }
  return result;
}

void UiScrollBar::draw(SDL_Renderer * renderer, SDL_FRect * layout_rect) {
  (void) layout_rect;
  if (!this->has_scroll_rect) {
    // The position is set by the element being scrolled, nothing to draw yet
    return;
  }

  if (this->background != nullptr) {
    this->background->draw(renderer, &this->draw_rect);
  }
  if (this->up_arrow != nullptr) {
    SDL_FRect up_arrow_rect = this->getUpArrowRect();
    this->up_arrow->draw(renderer, &up_arrow_rect);
  }
  if (this->down_arrow != nullptr) {
    SDL_FRect down_arrow_rect = this->getDownArrowRect();
    this->down_arrow->draw(renderer, &down_arrow_rect);
  }
  if (this->thumb != nullptr) {
    SDL_FRect thumb_rect = this->getThumbRect();
    this->thumb->draw(renderer, &thumb_rect);
  }
}
