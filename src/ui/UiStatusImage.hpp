#ifndef UI_STATUS_IMAGE_HPP
#define UI_STATUS_IMAGE_HPP

#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "UiImageSet.hpp"
#include "../engine/IniReader.hpp"
#include "../engine/ResourceManager.hpp"

// An image set which picks its image by comparing a value against the
// transition thresholds from the layout data, like the red/yellow/green
// status bars on the animal panel.
class UiStatusImage : public UiImageSet {
public:
  UiStatusImage(IniReader * ini_reader, ResourceManager * resource_manager, std::string name);

  void setValue(int value);

private:
  std::vector<int> transitions;
};

#endif // UI_STATUS_IMAGE_HPP
