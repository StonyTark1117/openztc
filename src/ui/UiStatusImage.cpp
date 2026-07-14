#include "UiStatusImage.hpp"

UiStatusImage::UiStatusImage(IniReader * ini_reader, ResourceManager * resource_manager, std::string name) : UiImageSet(ini_reader, resource_manager, name) {
  this->transitions = ini_reader->getIntList(name, "transition");
}

void UiStatusImage::setValue(int value) {
  int index = 0;
  for (int transition : this->transitions) {
    if (value < transition) {
      break;
    }
    index++;
  }
  this->setIndex(index);
}
