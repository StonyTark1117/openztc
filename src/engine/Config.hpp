#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <vector>
#include <string>

#include <SDL3/SDL.h>

#include "IniReader.hpp"

class Config {
public:
  Config(const std::string &filename = "zoo.ini");
  ~Config();

  std::vector<std::string> getResourcePaths();
  std::string getMenuMusic();
  bool getPlayMenuMusic();
  int getScreenWidth();
  int getScreenHeight();
  std::string getLangDllName();
  std::string getResDllName();
  SDL_Color getProgressColor();
  SDL_FRect getProgressPosition();
  // The freeform starting cash spinner settings from the UI section
  int getFreeformStartingCash();
  int getFreeformCashIncrement();
  int getFreeformCashMin();
  int getFreeformCashMax();
  // The original writes the chosen amount back to zoo.ini, so it persists
  // between sessions. Only that one line changes, the rest of the file is
  // preserved as it is.
  void setFreeformStartingCash(int value);
  // Whether to keep the display awake while the game runs. An openztc
  // section option in zoo.ini, on unless keepDisplayAwake=0 is set.
  bool getKeepDisplayAwake();
private:
  IniReader * reader = NULL;
  std::string file_path = "";
};

#endif // CONFIG_HPP