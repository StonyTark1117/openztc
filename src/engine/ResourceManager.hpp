#ifndef RESOURCE_MANAGER_HPP
#define RESOURCE_MANAGER_HPP

#include <unordered_map>
#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_mixer/SDL_mixer.h>

#include "Config.hpp"
#include "ZtdFile.hpp"
#include "IniReader.hpp"
#include "PeFile.hpp"
#include "AniFile.hpp"
#include "Animation.hpp"
#include "FontManager.hpp"
#include "Pallet.hpp"
#include "PalletManager.hpp"


class ResourceManager {
public:
  ResourceManager(Config * config);
  ~ResourceManager();

  Config * getConfig() { return this->config; }

  // Mod archives load before the game archives, so their resources win.
  // Must be set before load_resource_map runs.
  void setModArchives(const std::vector<std::string> &archives) { this->mod_archives = archives; }

  void load_resource_map(std::atomic<float> * progress, std::atomic<bool> * is_done);
  void load_string_map(std::atomic<float> * progress, std::atomic<bool> * is_done);
  void load_animation_map(std::atomic<float> * progress, std::atomic<bool> * is_done);
  void load_pallet_map(std::atomic<float> * progress, std::atomic<bool> * is_done);

  void * getFileContent(const std::string &file_name, int * size);
  SDL_Texture * getTexture(SDL_Renderer * renderer, const std::string &file_name);
  SDL_Cursor * getCursor(uint32_t cursor_id);
  MIX_Audio * getMusic(const std::string &file_name);
  IniReader * getIniReader(const std::string &file_name);
  Animation * getAnimation(const std::string &file_name);
  SDL_Texture * getLoadTexture(SDL_Renderer * renderer);
  SDL_Texture * getStringTexture(SDL_Renderer * renderer, const int font, const std::string &string, SDL_Color color);
  SDL_Texture * getWrappedStringTexture(SDL_Renderer * renderer, const int font, const std::string &string, SDL_Color color, int wrap_width);
  int getFontLineHeight(const int font);
  std::string getString(uint32_t string_id);
  std::string getTextFileContent(const std::string &file_name);
  std::vector<std::string> getResourceNamesWithExtension(const std::string &extension);

  void PlayMenuMusic();

private:
  std::unordered_map<std::string, std::string> resource_map;
  std::unordered_map<uint32_t, std::string> string_map;
  std::unordered_map<std::string, Animation *> animation_map;
  std::unordered_map<std::string, Pallet *> pallet_map;
  bool resource_map_loaded = false;

  std::string getResourceLocation(const std::string &resoure_name, bool failure_is_critical=true);

  MIX_Mixer * mixer = nullptr;
  MIX_Audio * menu_music = nullptr;
  MIX_Track * menu_music_track = nullptr;


  Config * config;
  std::vector<std::string> mod_archives;
  FontManager font_manager;
  PalletManager pallet_manager;
};

#endif // RESOURCE_MANAGER_HPP