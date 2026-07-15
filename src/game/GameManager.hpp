#ifndef GAME_MANAGER_HPP
#define GAME_MANAGER_HPP

#include <unordered_map>
#include <vector>
#include <atomic>

#include "../engine/IniReader.hpp"
#include "../engine/ModManager.hpp"
#include "../engine/ResourceManager.hpp"
#include "../engine/CursorManager.hpp"
#include "../ui/UiLayout.hpp"
#include "MapView.hpp"
#include "Simulation.hpp"
#include "../ui/UiAction.hpp"

class GameManager {
public:
  GameManager(ResourceManager * resource_manager, CursorManager * cursor_manager, ModManager * mod_manager = nullptr);
  ~GameManager();

  bool HandleInputs(std::vector<Input> &inputs);
  void Draw(SDL_Renderer * renderer, SDL_FRect * window_rect);

  void Load(std::atomic<float> * progress, std::atomic<bool> * is_done);

  // Advances the active game by one fixed tick, a no-op outside a map or
  // while the game is paused
  void TickSimulation();

private:
  std::unordered_map<std::string, UiLayout*> layouts;
  int id = 0;
  bool loaded = false;
  ResourceManager * resource_manager = nullptr;
  CursorManager * cursor_manager = nullptr;
  ModManager * mod_manager = nullptr;

  size_t credits_page = 0;
  uint64_t credits_page_start = 0;
  bool credits_active = false;

  typedef struct {
    std::string file;
    std::string picture;
    std::string story_file;
  } ScenarioInfo;

  typedef struct {
    std::string icon;
    std::string description_file;
    std::string savegame;
  } FreeformMapInfo;

  std::vector<ScenarioInfo> scenarios;
  std::vector<FreeformMapInfo> freeform_maps;
  int starting_cash = 0;

  MapView * map_view = nullptr;
  Simulation * simulation = nullptr;
  // Like the original, a loaded map starts paused
  bool simulation_paused = true;
  int shown_month = -1;
  int shown_year = -1;
  int64_t shown_cash = -1;

  bool handleTargetlessAction(UiAction);
  void startFreeformMap();
  void updateCreditsPages();
  void loadScenarioList();
  void loadFreeformMapList();
  void setupModScreen();
  void refreshModList();
  void showSelectedMod();
  void updateGameHud(SDL_FRect * window_rect);
  void setupGameHud();
  void leaveMap();
  std::string formatMoney(int64_t amount);
  void showSelectedScenario();
  void showSelectedFreeformMap();
  void changeStartingCash(int amount);
  void updateStartingCashText();
  void fillObjectivePlaceholder(std::string &text, int value);
};

#endif // GAME_MANAGER_HPP