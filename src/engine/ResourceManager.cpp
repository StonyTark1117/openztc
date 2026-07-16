#include "ResourceManager.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

#include <SDL3/SDL.h>

#include "ZtdFile.hpp"
#include "Utils.hpp"
#include "FontManager.hpp"
#include "Expansion.hpp"

ResourceManager::ResourceManager(Config * config) : config(config) {

}

ResourceManager::~ResourceManager() {
  // MIX_StopTrack(this->menu_music);
  if (this->menu_music != nullptr){
    MIX_DestroyAudio(this->menu_music);
  }
  if (this->menu_music_track != nullptr) {
    MIX_DestroyTrack(this->menu_music_track);
  }
  if (this->mixer != nullptr) {
    // The manager can be torn down and rebuilt for a mod reload
    MIX_DestroyMixer(this->mixer);
  }
  for (auto animation_entry : this->animation_map) {
    if (animation_entry.second != nullptr) {
      delete animation_entry.second;
    }
  }
}

std::string ResourceManager::getResourceLocation(const std::string &resource_name, bool failure_is_critical) {
  if (!this->resource_map.contains(resource_name)) {
    // Try with a slash behind it
    std::string resource_name_with_slash = resource_name + "/";
    if(this->resource_map.contains(resource_name_with_slash)) {
      return this->resource_map[resource_name_with_slash];
    }
    // If we got some weird list, just take the first item
    int semicolon_position = resource_name.find(";");
    std::string resource_name_no_list = resource_name.substr(0, semicolon_position);
    if(this->resource_map.contains(resource_name_no_list)) {
      return this->resource_map[resource_name_no_list];
    }
    // Give up
    if (failure_is_critical) {
      SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Could not find resource %s", resource_name.c_str());
    } else {
      #ifdef DEBUG
        SDL_Log("Could not find resource %s", resource_name.c_str());
      #endif
    }
    return "";
  }

  return this->resource_map[resource_name];
}

void ResourceManager::load_resource_map(std::atomic<float> * progress, std::atomic<bool> * is_done) {
  if (this->resource_map_loaded) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,"Resource maps was already loaded");
    *progress = 100.0f;
    *is_done = true;
    return;
  }
  std::string current_archive = "";

  SDL_Log("Loading resource map...");

  // Mods load first, so their resources shadow the game archives. Their
  // order is the load order from the mod manager.
  for (std::string archive : this->mod_archives) {
    SDL_Log("Loading mod archive %s", archive.c_str());
    for (std::string file : ZtdFile::getFileList(archive)) {
      if (resource_map.count(file) == 0) {
        resource_map[file] = archive;
        if (Utils::getFileExtension(file) == "PAL") {
          this->pallet_manager.addPalletFileToMap(file, archive);
        }
      }
    }
  }

  std::vector<std::string> resource_paths = config->getResourcePaths();
  float progress_per_resource_path_load = (100.0f - *progress) / (float) resource_paths.size();
  for (std::string path : resource_paths) {
    SDL_Log("Resource path %s", path.c_str());
    path = Utils::fixPath(path);
    SDL_Log("Resource path after fixPath %s", path.c_str());
    if (path.empty())
      continue;
    // Sort the archives so which archive provides a resource does not depend
    // on the unspecified directory iteration order
    std::vector<std::string> archives;
    for (std::filesystem::directory_entry archive : std::filesystem::directory_iterator(path)) {
      if (Utils::getFileExtension(archive.path().string()) == "ZTD") {
        archives.push_back(archive.path().string());
      }
    }
    std::sort(archives.begin(), archives.end());
    for (std::string archive : archives) {
      current_archive = archive;
      for (std::string file : ZtdFile::getFileList(current_archive)) {
        if (resource_map.count(file) == 0) {
          resource_map[file] = current_archive;
          if(Utils::getFileExtension(file) == "PAL") {
            this->pallet_manager.addPalletFileToMap(file, current_archive);
          }
        }
      }
    }

    // Increase progress bar position
    if (*progress + progress_per_resource_path_load < 100.0f) {
      *progress = *progress + progress_per_resource_path_load;
    } else {
      *progress = 100.0f;
    }
  }
  this->resource_map_loaded = true;
  SDL_Log("Loading resource map done");
  *is_done = true;
}

void ResourceManager::load_string_map(std::atomic<float> * progress, std::atomic<bool> * is_done) {
  std::vector<std::string> lang_dlls;
  for (std::filesystem::directory_entry lang_dll : std::filesystem::directory_iterator(Utils::getZooTycoonPath())) {
    std::string current_dll = lang_dll.path().filename().string();
    if (!Utils::string_to_lower(current_dll).starts_with("lang") || Utils::getFileExtension(current_dll) != "DLL") {
      continue;
    }
    lang_dlls.push_back(lang_dll.path().string());
  }
  std::sort(lang_dlls.begin(), lang_dlls.end());

  float progress_per_dll_load = (100.0f - *progress) / (float) lang_dlls.size();
  for (std::string lang_dll : lang_dlls) {
    SDL_Log("Loading strings from %s", lang_dll.c_str());
    PeFile pe_file(lang_dll);
    for (uint32_t string_id : pe_file.getStringIds()) {
      std::string string = pe_file.getString(string_id);
      if (!string.empty()) {
        this->string_map[string_id] = string;
      }
    }

    // Increase progress bar position
    if (*progress + progress_per_dll_load < 100.0f) {
      *progress = *progress + progress_per_dll_load;
    } else {
      *progress = 100.0f;
    }
  }
  SDL_Log("Loaded %i strings", (int) this->string_map.size());
  *is_done = true;
}

void ResourceManager::load_animation_map(std::atomic<float> * progress, std::atomic<bool> * is_done) {
  std::vector<std::string> ani_files;
  for (auto file : this->resource_map) {
    std::string file_name = file.first;
    if(file_name.ends_with(".ani")) {
      ani_files.push_back(file_name);
    }
  }

  float progress_per_animation_load = (100.0f - *progress) / (float) ani_files.size();
  for (size_t i = 0; i < ani_files.size(); i++) {
    SDL_Log("Loading animation from %s", ani_files[i].c_str());
    this->animation_map[ani_files[i]] = this->getAnimation(ani_files[i]);

    // Increase progress bar position
    if (*progress + progress_per_animation_load < 100.0f) {
      *progress = *progress + progress_per_animation_load;
    } else {
      *progress = 100.0f;
    }
  }
  *is_done = true;
}

void ResourceManager::load_pallet_map(std::atomic<float> * progress, std::atomic<bool> * is_done) {
  this->pallet_manager.loadPalletMap(progress, is_done);
}

void * ResourceManager::getFileContent(const std::string &file_name, int *size) {
  return ZtdFile::getFileContent(getResourceLocation(file_name), file_name, size);
}

SDL_Texture * ResourceManager::getTexture(SDL_Renderer * renderer, const std::string &file_name) {
  SDL_Texture * texture = nullptr;
  SDL_Surface * surface = ZtdFile::getImageSurface(getResourceLocation(file_name), file_name);
  if (surface != nullptr) {
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
  }
  return texture;
}

SDL_Cursor * ResourceManager::getCursor(uint32_t cursor_id) {
  PeFile pe_file(this->config->getResDllName());

  SDL_Surface * surface = pe_file.getCursor(cursor_id);
  if (surface == nullptr) {
    return nullptr;
  }
  SDL_Cursor * cursor = SDL_CreateColorCursor(surface, 0, 0);
  SDL_DestroySurface(surface);

  return cursor;
}

MIX_Audio * ResourceManager::getMusic(const std::string &file_name) {
  return ZtdFile::getMusic(getResourceLocation(file_name), file_name, this->mixer);
}

IniReader * ResourceManager::getIniReader(const std::string &file_name) {
  return ZtdFile::getIniReader(getResourceLocation(file_name), file_name);
}

Animation *ResourceManager::getAnimation(const std::string &file_name) {
  return this->getAnimation(file_name, "", 0);
}

Animation *ResourceManager::getAnimation(const std::string &file_name, const std::string &replacement_pallet_name,
                                         int replacement_start) {
  // Animations are cached and owned by the resource manager, so elements
  // using the same animation share one instance. Recolored variants cache
  // under their own key.
  // Art with two recolorable ramps names both palettes separated by a
  // semicolon; the first ramp starts at replacement_start and the second
  // follows it directly (232 fixed + 16 + 8 fills the 256 exactly on the
  // scn08 stands)
  const Pallet * replacement_pallet = nullptr;
  std::string cache_key = file_name;
  if (!replacement_pallet_name.empty()) {
    std::string pallet_name = replacement_pallet_name;
    size_t semicolon = pallet_name.find(';');
    if (semicolon == std::string::npos) {
      replacement_pallet = this->pallet_manager.getPallet(pallet_name);
    } else {
      // Art with two recolorable ramps names both palettes separated by a
      // semicolon. The ramps sit back to back in the art's pallet (232
      // fixed + 16 + 8 fills the 256 exactly on the scn08 stands), so a
      // composed replacement carries both after one sentinel.
      std::string first_name = pallet_name.substr(0, semicolon);
      std::string second_name = pallet_name.substr(semicolon + 1);
      Pallet * first = this->pallet_manager.getPallet(first_name);
      Pallet * second = this->pallet_manager.getPallet(second_name);
      if (first != nullptr) {
        Pallet &composed = this->composed_pallets[pallet_name];
        composed = *first;
        if (second != nullptr) {
          for (uint32_t i = 1; i < second->color_count && composed.color_count < 256; i++) {
            composed.colors[composed.color_count++] = second->colors[i];
          }
        }
        replacement_pallet = &composed;
      }
    }
    if (replacement_pallet != nullptr) {
      cache_key += ";" + replacement_pallet_name + "@" + std::to_string(replacement_start);
    }
  }
  if (this->animation_map.contains(cache_key)) {
    return this->animation_map[cache_key];
  }

  Animation * animation = nullptr;
  std::string resource_location = getResourceLocation(file_name, false);
  if (!resource_location.empty()) {
    animation = AniFile::getAnimation(&this->pallet_manager, resource_location, file_name, replacement_pallet, replacement_start);
  } else if (file_name.find(";") != std::string::npos) {
    int semicolon_position = file_name.find(";");
    std::string full_file_name = file_name.substr(0, semicolon_position);
    if (!full_file_name.ends_with(".ani")) {
      full_file_name += ".ani";
    }
    resource_location = getResourceLocation(full_file_name, false);
    animation = AniFile::getAnimation(&this->pallet_manager, resource_location, full_file_name, replacement_pallet, replacement_start);
  } else {
    std::string full_file_name = file_name + ".ani";
    resource_location = getResourceLocation(full_file_name);
    animation = AniFile::getAnimation(&this->pallet_manager, resource_location, full_file_name, replacement_pallet, replacement_start);
  }
  if (animation != nullptr) {
    this->animation_map[cache_key] = animation;
  }
  return animation;
}

SDL_Texture * ResourceManager::getLoadTexture(SDL_Renderer *renderer) {
  Expansion expansion = Utils::getExpansion();
  uint32_t loading_screen_id = 502;
  std::string lang_dll_name = Utils::getExpansionLangDllName(expansion);

  if (expansion == Expansion::ALL) {
    loading_screen_id = 505;
  } else if (expansion == Expansion::MARINE_MANIA) {
    loading_screen_id = 504;
  }

  PeFile pe_file(lang_dll_name);
  SDL_Surface * surface = pe_file.getLoadScreenSurface(loading_screen_id);
  SDL_Texture * texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_DestroySurface(surface);
  return texture;
}

SDL_Texture *ResourceManager::getStringTexture(SDL_Renderer * renderer, const int font, const std::string &string, SDL_Color color) {
  return this->font_manager.getStringTexture(renderer, font, string, color);
}

std::string ResourceManager::getString(uint32_t string_id) {
  return this->string_map[string_id];
}

SDL_Texture * ResourceManager::getWrappedStringTexture(SDL_Renderer * renderer, const int font, const std::string &string, SDL_Color color, int wrap_width) {
  return this->font_manager.getWrappedStringTexture(renderer, font, string, color, wrap_width);
}

int ResourceManager::getFontLineHeight(const int font) {
  return this->font_manager.getFontLineHeight(font);
}

std::string ResourceManager::getTextFileContent(const std::string &file_name) {
  std::string resource_location = this->getResourceLocation(file_name, false);
  if (resource_location.empty()) {
    return "";
  }
  int size = 0;
  void * content = ZtdFile::getFileContent(resource_location, file_name, &size);
  if (content == nullptr) {
    return "";
  }
  std::string text = std::string((char *) content, (size_t) size);
  free(content);
  // The text files use Windows line endings
  std::string result;
  result.reserve(text.length());
  for (char character : text) {
    if (character != '\r') {
      result += character;
    }
  }
  return result;
}

std::vector<std::string> ResourceManager::getResourceNamesWithExtension(const std::string &extension) {
  std::vector<std::string> resource_names;
  for (auto resource_entry : this->resource_map) {
    if (Utils::getFileExtension(resource_entry.first) == extension) {
      resource_names.push_back(resource_entry.first);
    }
  }
  return resource_names;
}

void ResourceManager::PlayMenuMusic() {
  if(!this->resource_map_loaded || !this->config->getPlayMenuMusic()) {
    return;
  }
  if (this->mixer == nullptr) {
    this->mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!this->mixer) {
      SDL_Log("Couldn't create mixer on default device: %s", SDL_GetError());
      return;
    }
  }
  if (this->menu_music == nullptr) {
    this->menu_music = this->getMusic(this->config->getMenuMusic());
  }
  SDL_PropertiesID options = SDL_CreateProperties();
  SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
  if(this->menu_music_track == nullptr) {
    this->menu_music_track = MIX_CreateTrack(mixer);
  }
  MIX_SetTrackAudio(this->menu_music_track, this->menu_music);
  MIX_PlayTrack(this->menu_music_track, options);
  SDL_DestroyProperties(options);
}
