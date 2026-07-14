#ifndef GAME_MANAGER_HPP
#define GAME_MANAGER_HPP

#include <unordered_map>
#include <vector>
#include <atomic>

#include "IniReader.hpp"
#include "ResourceManager.hpp"
#include "CursorManager.hpp"
#include "ui/UiLayout.hpp"
#include "ui/UiAction.hpp"

class GameManager {
public:
  GameManager(ResourceManager * resource_manager, CursorManager * cursor_manager);
  ~GameManager();

  bool HandleInputs(std::vector<Input> &inputs);
  void Draw(SDL_Renderer * renderer, SDL_FRect * window_rect);

  void Load(std::atomic<float> * progress, std::atomic<bool> * is_done);

private:
  std::unordered_map<std::string, UiLayout*> layouts;
  int id = 0;
  bool loaded = false;
  ResourceManager * resource_manager = nullptr;
  CursorManager * cursor_manager = nullptr;

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
    int starting_cash;
  } FreeformMapInfo;

  std::vector<ScenarioInfo> scenarios;
  std::vector<FreeformMapInfo> freeform_maps;
  int starting_cash = 0;

  bool handleTargetlessAction(UiAction);
  void updateCreditsPages();
  void loadScenarioList();
  void loadFreeformMapList();
  void showSelectedScenario();
  void showSelectedFreeformMap();
  void changeStartingCash(int amount);
  void updateStartingCashText();
  void fillObjectivePlaceholder(std::string &text, int value);
};

#endif // GAME_MANAGER_HPP