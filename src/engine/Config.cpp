#include "Config.hpp"

#include <fstream>

#include <SDL3/SDL.h>

#include "Utils.hpp"

Config::Config(const std::string &filename) {
  this->file_path = Utils::fixPath(Utils::getZooTycoonPath() + "/" + filename);
  this->reader = new IniReader(this->file_path);
}

Config::~Config(){

}

std::vector<std::string>  Config::getResourcePaths() {
  std::vector<std::string> resource_paths = std::vector<std::string>();

  std::string path_string = reader->get("resource", "path", "");
  std::string current_path = "";
  for (char character : path_string) {
    if (current_path.empty() && (character == '.' || character == '/')) {
      continue;
    }
    if (character == ';') {
      resource_paths.push_back(Utils::getZooTycoonPath() + "/" + current_path);
      current_path = "";
      continue;
    }
    current_path += character;
  }
  resource_paths.push_back(Utils::getZooTycoonPath() + "/" + current_path);

  for(std::string path : resource_paths) {
    SDL_Log("Found resource path %s", path.c_str());
  }

  return resource_paths;
}

std::string Config::getMenuMusic()
{
  return reader->get("ui", "menuMusic", "");
}

bool Config::getPlayMenuMusic()
{
  return (reader->get("ui", "noMenuMusic") == "0");
}

int Config::getScreenWidth()
{
  return reader->getInt("user", "screenwidth", 800);
}

int Config::getScreenHeight()
{
  return reader->getInt("user", "screenheight", 600);
}

std::string Config::getLangDllName() {
  return reader->get("lib", "lang");
}

std::string Config::getResDllName() {
  return reader->get("lib", "res");
}

SDL_Color Config::getProgressColor() {
  SDL_Color color;

  color.r = (uint8_t) reader->getInt("UI", "progressRed", 255);
  color.g = (uint8_t) reader->getInt("UI", "progressGreen", 255);
  color.b = (uint8_t) reader->getInt("UI", "progressBlue", 255);
  color.a = 255;

  return color;
}

SDL_FRect Config::getProgressPosition() {
  SDL_FRect rect;

  rect.x = (float) reader->getInt("UI", "progressLeft", 0);
  rect.y = (float) reader->getInt("UI", "progressTop", 0);
  rect.w = (float) reader->getInt("UI", "progressRight", 0) - rect.x;
  rect.h = (float) reader->getInt("UI", "progressBottom", 0) - rect.y;

  return rect;
}
// The freeform cash spinner settings. The fallbacks are the values the
// original game ships with in zoo.ini.
int Config::getFreeformStartingCash() {
  return reader->getInt("UI", "MSStartingCash", 75000);
}

int Config::getFreeformCashIncrement() {
  return reader->getInt("UI", "MSCashIncrement", 5000);
}

int Config::getFreeformCashMin() {
  return reader->getInt("UI", "MSMinCash", 10000);
}

int Config::getFreeformCashMax() {
  return reader->getInt("UI", "MSMaxCash", 500000);
}

void Config::setFreeformStartingCash(int value) {
  // Rewrite only the MSStartingCash line inside the UI section, keeping
  // every other byte of the user's zoo.ini as it is. The file is written
  // in place because it may be reached through a symlink.
  std::ifstream in(this->file_path, std::ios::binary);
  if (!in.is_open()) {
    SDL_Log("Could not open %s to persist the starting cash", this->file_path.c_str());
    return;
  }
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();

  std::string output;
  output.reserve(content.length() + 16);
  bool in_ui_section = false;
  bool replaced = false;
  size_t position = 0;
  while (position < content.length()) {
    size_t line_end = content.find('\n', position);
    size_t next = line_end == std::string::npos ? content.length() : line_end + 1;
    std::string line = content.substr(position, next - position);
    std::string trimmed = line;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
      trimmed.pop_back();
    }
    std::string lower = Utils::string_to_lower(trimmed);
    if (!lower.empty() && lower[0] == '[') {
      in_ui_section = lower == "[ui]";
    } else if (in_ui_section && !replaced && lower.rfind("msstartingcash", 0) == 0) {
      // Keep the line ending style of the original line
      std::string ending = line.substr(trimmed.length());
      line = "MSStartingCash=" + std::to_string(value) + ending;
      replaced = true;
    }
    output += line;
    position = next;
  }
  if (!replaced) {
    SDL_Log("No MSStartingCash line in %s, not persisting the starting cash", this->file_path.c_str());
    return;
  }

  std::ofstream out(this->file_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    SDL_Log("Could not write %s to persist the starting cash", this->file_path.c_str());
    return;
  }
  out << output;
}
