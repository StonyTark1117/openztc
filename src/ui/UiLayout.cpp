#include "UiLayout.hpp"

#include <algorithm>

#include "UiImage.hpp"
#include "UiText.hpp"
#include "UiButton.hpp"
#include "UiListBox.hpp"
#include "UiScrollBar.hpp"
#include "UiEditableText.hpp"
#include "UiImageSet.hpp"
#include "UiStatusImage.hpp"
#include "UiRadioSet.hpp"

UiLayout::UiLayout(IniReader * ini_reader, ResourceManager * resource_manager, CursorManager * cursor_manager) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->cursor_manager = cursor_manager;
  this->process_sections();
}

UiLayout::UiLayout(IniReader *ini_reader, ResourceManager *resource_manager, CursorManager * cursor_manager, std::string name) {
  this->name = name;
  this->active = ini_reader->getInt(this->name, "state", 0) != 1;
  this->resource_manager = resource_manager;
  this->cursor_manager = cursor_manager;
  this->process_layout(ini_reader->get(this->name, "layout"));
}

UiLayout::~UiLayout() {
    for (UiElement * element : this->children) {
      delete element;
    }
    if (this->ini_reader != nullptr) {
      delete this->ini_reader;
    }
}

void UiLayout::draw(SDL_Renderer *renderer, SDL_FRect * layout_rect) {
  this->generateDrawRect(this->ini_reader->getSection("layoutinfo"), layout_rect);
  drawChildren(renderer, &this->draw_rect);
}

void UiLayout::process_sections() {
  for(std::string section: this->ini_reader->getSections()) {
    if (section == "layoutinfo") {
      this->id = this->ini_reader->getInt(section, "id", 0);
      this->layer = this->ini_reader->getInt(section, "layer", 0);
      continue;
    }

    std::string element_type = this->ini_reader->get(section, "type");
    if (element_type.empty()) {
      SDL_Log("Could not determine type of section %s in layout %s", section.c_str(), this->name.c_str());
    } else if (element_type == "UIImage") {
      this->children.push_back((UiElement *) new UiImage(this->ini_reader, this->resource_manager, section));
    } else if (element_type == "UIButton") {
      this->children.push_back((UiElement *) new UiButton(this->ini_reader, this->resource_manager, this->cursor_manager, section));
    } else if (element_type == "UIText") {
      this->children.push_back((UiElement *) new UiText(this->ini_reader, this->resource_manager, section));
    } else if (element_type == "UILayout") {
      this->children.push_back((UiElement *) new UiLayout(this->ini_reader, this->resource_manager, this->cursor_manager, section));
    } else if (element_type == "UIListBox") {
      this->children.push_back((UiElement *) new UiListBox(this->ini_reader, this->resource_manager, section));
    } else if (element_type == "UIScrollBar") {
      this->children.push_back((UiElement *) new UiScrollBar(this->ini_reader, this->resource_manager, section));
    } else if (element_type == "UIEditableText") {
      this->children.push_back((UiElement *) new UiEditableText(this->ini_reader, this->resource_manager, section));
    } else if (element_type == "UIImageSet") {
      this->children.push_back((UiElement *) new UiImageSet(this->ini_reader, this->resource_manager, section));
    } else if (element_type == "UIStatusImage") {
      this->children.push_back((UiElement *) new UiStatusImage(this->ini_reader, this->resource_manager, section));
    } else if (element_type == "UIRadioSet") {
      UiRadioSet * radio_set = new UiRadioSet(this->ini_reader, this->resource_manager, section);
      this->children.push_back((UiElement *) radio_set);
      this->radio_sets.push_back(radio_set);
    } else {
      SDL_Log("Support for element type %s is not yet implemented", element_type.c_str());
    }
  }

  // Link radio sets to the buttons they reference by id
  for (UiRadioSet * radio_set : this->radio_sets) {
    for (int button_id : radio_set->getButtonIds()) {
      for (UiElement * element : this->children) {
        UiButton * button = dynamic_cast<UiButton*>(element);
        if (button != nullptr && button->getId() == button_id) {
          radio_set->addButton(button);
          break;
        }
      }
    }
  }

  // Link list boxes and text boxes to the scroll bar they reference by id
  for (UiElement * element : this->children) {
    UiListBox * list_box = dynamic_cast<UiListBox*>(element);
    UiText * text = dynamic_cast<UiText*>(element);
    int scroll_bar_id = 0;
    if (list_box != nullptr) {
      scroll_bar_id = list_box->getScrollBarId();
    } else if (text != nullptr) {
      scroll_bar_id = text->getScrollBarId();
    }
    if (scroll_bar_id == 0) {
      continue;
    }
    for (UiElement * other : this->children) {
      UiScrollBar * scroll_bar = dynamic_cast<UiScrollBar*>(other);
      if (scroll_bar != nullptr && scroll_bar->getId() == scroll_bar_id) {
        if (list_box != nullptr) {
          list_box->setScrollBar(scroll_bar);
        } else {
          text->setScrollBar(scroll_bar);
        }
        break;
      }
    }
  }
}

void UiLayout::process_layout(std::string layout) {
  if (layout.empty()) {
    return;
  }
  if (layout != "ui/infocomC.lyt") {
    // infocomC.lyt is the only lyt file that has a capital letter in the name
    std::transform(layout.begin(), layout.end(), layout.begin(), ::tolower);
  }
  this->ini_reader = this->resource_manager->getIniReader(layout);
  process_sections();
}

UiAction UiLayout::handleInputs(std::vector<Input> &inputs) {
  UiAction result = handleInputChildren(inputs);
  // When a clicked button is part of a radio set, it becomes the selected
  // button of that set
  if (result.source != 0) {
    for (UiRadioSet * radio_set : this->radio_sets) {
      if (radio_set->hasButton(result.source)) {
        radio_set->select(result.source);
        break;
      }
    }
  }
  return result;
}

std::vector<UiLayout*> UiLayout::getChildLayouts() {
  std::vector<UiLayout*> child_layouts;
  for (UiElement * child : this->children) {
    UiLayout * child_layout = dynamic_cast<UiLayout*>(child);
    if (child_layout != nullptr) {
      child_layouts.push_back(child_layout);
    }
  }
  return child_layouts;
}
