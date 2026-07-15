#include "ModManager.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <SDL3/SDL.h>

#include "Utils.hpp"

#define STATE_FILE_NAME "openztc-mods.txt"
#define LOADOUT_FILE_PREFIX "openztc-loadout-"

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

std::vector<std::string> ModManager::getConflicts(int index) {
  std::vector<std::string> conflicts;
  if (!this->archive_lister || index < 0 || index >= (int) this->mods.size()) {
    return conflicts;
  }
  std::vector<std::string> own_files = this->archive_lister(this->mods_directory + "/" + this->mods[index].file_name);
  std::sort(own_files.begin(), own_files.end());
  for (int other = 0; other < (int) this->mods.size(); other++) {
    if (other == index || !this->mods[other].enabled) {
      continue;
    }
    for (std::string file : this->archive_lister(this->mods_directory + "/" + this->mods[other].file_name)) {
      if (file.empty() || file.back() == '/') {
        // Directory entries are not resources
        continue;
      }
      if (std::binary_search(own_files.begin(), own_files.end(), file)) {
        conflicts.push_back(this->mods[other].file_name);
        break;
      }
    }
  }
  return conflicts;
}

std::vector<std::string> ModManager::listLoadouts() {
  std::vector<std::string> names;
  if (!std::filesystem::is_directory(this->mods_directory)) {
    return names;
  }
  for (std::filesystem::directory_entry entry : std::filesystem::directory_iterator(this->mods_directory)) {
    std::string file_name = entry.path().filename().string();
    if (entry.is_regular_file() && file_name.rfind(LOADOUT_FILE_PREFIX, 0) == 0 && file_name.ends_with(".txt")) {
      names.push_back(file_name.substr(strlen(LOADOUT_FILE_PREFIX), file_name.length() - strlen(LOADOUT_FILE_PREFIX) - 4));
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

bool ModManager::applyLoadout(const std::string &name) {
  std::ifstream loadout(this->mods_directory + "/" + LOADOUT_FILE_PREFIX + name + ".txt");
  if (!loadout.is_open()) {
    return false;
  }
  // The loadout's order and flags apply to the mods that still exist,
  // anything it does not mention keeps its place after them
  std::vector<ModInfo> ordered;
  std::string line;
  while (std::getline(loadout, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    if (line.length() < 2 || (line[0] != '+' && line[0] != '-')) {
      continue;
    }
    std::string file_name = line.substr(1);
    for (size_t i = 0; i < this->mods.size(); i++) {
      if (this->mods[i].file_name == file_name) {
        ordered.push_back({file_name, line[0] == '+'});
        this->mods.erase(this->mods.begin() + i);
        break;
      }
    }
  }
  ordered.insert(ordered.end(), this->mods.begin(), this->mods.end());
  this->mods = ordered;
  this->save();
  return true;
}
