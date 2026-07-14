#include "ModManager.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <SDL3/SDL.h>

#include "Utils.hpp"

#define STATE_FILE_NAME "openztc-mods.txt"

ModManager::ModManager(const std::string &mods_directory) {
  this->mods_directory = mods_directory;
  this->state_file = mods_directory + "/" + STATE_FILE_NAME;
}

void ModManager::load() {
  this->mods.clear();
  if (!std::filesystem::is_directory(this->mods_directory)) {
    SDL_Log("No mods directory at %s", this->mods_directory.c_str());
    return;
  }

  // The state file lists one mod per line in load order, prefixed with +
  // for enabled and - for disabled
  std::ifstream state(this->state_file);
  std::string line;
  while (std::getline(state, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    if (line.length() < 2 || (line[0] != '+' && line[0] != '-')) {
      continue;
    }
    std::string file_name = line.substr(1);
    if (std::filesystem::is_regular_file(this->mods_directory + "/" + file_name)) {
      this->mods.push_back({file_name, line[0] == '+'});
    }
  }

  // Mods that are not in the state file yet get appended enabled
  std::vector<std::string> found;
  for (std::filesystem::directory_entry entry : std::filesystem::directory_iterator(this->mods_directory)) {
    if (!entry.is_regular_file() || Utils::getFileExtension(entry.path().string()) != "ZTD") {
      continue;
    }
    found.push_back(entry.path().filename().string());
  }
  std::sort(found.begin(), found.end());
  for (const std::string &file_name : found) {
    bool known = false;
    for (const ModInfo &mod : this->mods) {
      if (mod.file_name == file_name) {
        known = true;
        break;
      }
    }
    if (!known) {
      this->mods.push_back({file_name, true});
    }
  }

  SDL_Log("Found %i mods in %s", (int) this->mods.size(), this->mods_directory.c_str());
}

void ModManager::save() {
  if (!std::filesystem::is_directory(this->mods_directory)) {
    return;
  }
  std::ofstream state(this->state_file, std::ios::trunc);
  if (!state.is_open()) {
    SDL_Log("Could not write mod state to %s", this->state_file.c_str());
    return;
  }
  state << "; OpenZTC mod list, one mod per line in load order.\n";
  state << "; + is enabled, - is disabled. Earlier mods win conflicts.\n";
  for (const ModInfo &mod : this->mods) {
    state << (mod.enabled ? '+' : '-') << mod.file_name << "\n";
  }
}

void ModManager::toggle(int index) {
  if (index < 0 || index >= (int) this->mods.size()) {
    return;
  }
  this->mods[index].enabled = !this->mods[index].enabled;
  this->save();
}

bool ModManager::move(int index, int direction) {
  int other = index + direction;
  if (index < 0 || index >= (int) this->mods.size() || other < 0 || other >= (int) this->mods.size()) {
    return false;
  }
  std::swap(this->mods[index], this->mods[other]);
  this->save();
  return true;
}

std::vector<std::string> ModManager::getEnabledArchives() {
  std::vector<std::string> archives;
  for (const ModInfo &mod : this->mods) {
    if (mod.enabled) {
      archives.push_back(this->mods_directory + "/" + mod.file_name);
    }
  }
  return archives;
}
