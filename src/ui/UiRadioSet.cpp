#include "UiRadioSet.hpp"

UiRadioSet::UiRadioSet(IniReader * ini_reader, ResourceManager * resource_manager, std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;

  this->id = ini_reader->getInt(name, "id");
  this->layer = ini_reader->getInt(name, "layer", 1);
  this->button_ids = ini_reader->getIntList(name, "button");
  if (this->button_ids.empty() && ini_reader->getInt(name, "template", 0) != 0) {
    SDL_Log("Radio set %s uses a template button, which is not supported yet", name.c_str());
  }
}

UiRadioSet::~UiRadioSet() {
  for (UiElement * child : this->children) {
    delete child;
  }
}

std::vector<int> UiRadioSet::getButtonIds() {
  return this->button_ids;
}

void UiRadioSet::addButton(UiButton * button) {
  this->buttons.push_back(button);
}

bool UiRadioSet::hasButton(int button_id) {
  for (int id : this->button_ids) {
    if (id == button_id) {
      return true;
    }
  }
  return false;
}

void UiRadioSet::select(int button_id) {
  for (UiButton * button : this->buttons) {
    button->setRadioSelected(button->getId() == button_id);
  }
}

void UiRadioSet::draw(SDL_Renderer * renderer, SDL_FRect * layout_rect) {
  // The radio set has no visual representation of its own, the member
  // buttons draw themselves
  (void) renderer;
  (void) layout_rect;
}
