#include "GameManager.hpp"

#include <string>
#include <algorithm>
#include <tuple>

#include "../ui/UiListBox.hpp"
#include "../ui/UiImage.hpp"
#include "../ui/UiText.hpp"
#include "../ui/UiEditableText.hpp"
#include "../ui/UiButton.hpp"
#include "../ui/UiStatusImage.hpp"
#include "../ui/ZtMiniMap.hpp"

// The date and money displays in the in game toolbar, ui/main.lyt
#define HUD_DATE_ID 1030
#define HUD_MONEY_ID 1016
#define HUD_ZOO_STATUS_ID 1015
#define HUD_ANIMAL_STATUS_ID 1011
#define HUD_GUEST_STATUS_ID 1013
#define HUD_TREE_TOGGLE_ID 1066
#define HUD_BUILDING_TOGGLE_ID 1067
#define HUD_GUEST_TOGGLE_ID 1068
#define HUD_MINIMAP_ID 1026
#define HUD_PAUSE_BUTTON_ID 1071
#define HUD_PLAY_BUTTON_ID 1072
// The month names in the lang dlls, 22101 is Jan
#define MONTH_STRING_ID_BASE 22101

GameManager::GameManager(ResourceManager * resource_manager, CursorManager * cursor_manager, ModManager * mod_manager) {
  this->resource_manager = resource_manager;
  this->cursor_manager = cursor_manager;
  this->mod_manager = mod_manager;
  this->starting_cash = resource_manager->getConfig()->getFreeformStartingCash();
}

GameManager::~GameManager() {
  if (this->map_view != nullptr) {
    delete this->map_view;
  }
  if (this->simulation != nullptr) {
    delete this->simulation;
  }
  for(auto kv : layouts) {
    delete kv.second;
    layouts[kv.first] = nullptr;
  }
}

bool GameManager::HandleInputs(std::vector<Input> &inputs) {
  if (this->map_view != nullptr) {
    for (Input input : inputs) {
      if (input.event == InputEvent::PAUSE_TOGGLE) {
        this->simulation_paused = !this->simulation_paused;
      }
    }
    // The toolbar sees the inputs first: the minimap jumps the camera and
    // the pause button toggles, everything else is not wired up yet and
    // gets swallowed so half working panels do not open
    if (this->layouts.contains("main")) {
      UiAction hud_action = this->layouts["main"]->handleInputs(inputs);
      if (hud_action.source == HUD_MINIMAP_ID) {
        ZtMiniMap * minimap = dynamic_cast<ZtMiniMap*>(this->layouts["main"]->getChildWithId(HUD_MINIMAP_ID));
        float tile_x = 0.0f;
        float tile_y = 0.0f;
        if (minimap != nullptr && minimap->getClickedTile(&tile_x, &tile_y)) {
          this->map_view->lookAtTile(tile_x, tile_y);
        }
        return true;
      }
      if (hud_action.source == HUD_PAUSE_BUTTON_ID || hud_action.source == HUD_PLAY_BUTTON_ID) {
        this->simulation_paused = !this->simulation_paused;
        return true;
      }
      if (hud_action.source != 0) {
        return true;
      }
    }
    if (!this->map_view->handleInputs(inputs)) {
      this->leaveMap();
    }
    return true;
  }
  for (int layer=8; layer > (0 - 1); layer--) {
    for(auto kv : layouts) {
      UiLayout * layout = layouts[kv.first];
      if (layout == nullptr || layout->getLayer() != layer || !layout->getActive()) {
        continue;
      }
      UiAction action = layout->handleInputs(inputs);
      if (action.source != 0) {
        switch (action.action) {
          case Action::SHOW_TARGET_LAYOUT:
            for(auto kv : layouts) {
              UiLayout * layout = layouts[kv.first];
              if (layout->getId() == action.target) {
                layout->setActive(true);
              }
            }
            break;
          case Action::HIDE_TARGET_LAYOUT:
            for(auto kv : layouts) {
              UiLayout * layout = layouts[kv.first];
              if (layout->getId() == action.target) {
                layout->setActive(false);
              } else if (action.target == -1 && layout->hasId(action.source)) {
                layout->setActive(false);
              }
            }
            break;
          case Action::TOGGLE_TARGET_LAYOUT:
            for(auto kv : layouts) {
              UiLayout * layout = layouts[kv.first];
              if (layout->getId() == action.target) {
                layout->setActive(!layout->getActive());
              }
            }
            break;
          case Action::SWITCH_TO_TARGET_LAYOUT:
            for(auto kv : layouts) {
              UiLayout * layout = layouts[kv.first];
              if (layout->getId() == action.target) {
                layout->setActive(true);
              }
              if (layout->hasId(action.source)) {
                layout->setActive(false);
              }
            }
            break;
          case Action::NONE:
            {
              bool running = handleTargetlessAction(action);
              if (!running)
                return false;
            }
            break;
          default:
            break;
        }
        return true;
      }
    }
  }
  return true;
}

void GameManager::Draw(SDL_Renderer * renderer, SDL_FRect * window_rect) {
  if (this->map_view != nullptr) {
    if (this->simulation != nullptr) {
      this->map_view->setSimGuests(this->simulation->getGuests());
      this->map_view->setSimAnimals(this->simulation->getAnimals());
    }
    this->map_view->draw(renderer, window_rect);
    // The in game toolbar draws over the map. Only its date and money
    // displays are wired up so far.
    this->updateGameHud(window_rect);
    if (this->layouts.contains("main")) {
      this->layouts["main"]->draw(renderer, window_rect);
    }
    return;
  }
  this->updateCreditsPages();
  for (int layer=0; layer < (8 + 1); layer++) {
    for(auto kv : layouts) {
      UiLayout * layout = layouts[kv.first];
      if (layout == nullptr || layout->getLayer() != layer || !layout->getActive()) {
        continue;
      }
      layout->draw(renderer, window_rect);
    }
  }
}

void GameManager::Load(std::atomic<float> * progress, std::atomic<bool> * is_done) {
    if (this->loaded) {
    *progress = 100.0f;
    *is_done = true;
    return;
  }

  // Load all the layouts from ui/gamescrn.lyt
  IniReader * ini_reader = this->resource_manager->getIniReader("ui/gamescrn.lyt");
  float progress_per_layout_load = (100.0f - *progress) / (float) ini_reader->getSections().size();
  for(std::string section : ini_reader->getSections()) {
    if (section == "layoutinfo") {
      this->id = ini_reader->getInt(section, "id");
    } else {
      std::string type = ini_reader->get(section, "type");
      if (type == "UILayout") {
        #ifdef DEBUG
          SDL_Log("Loading layout %s at %s", section.c_str(), ini_reader->get(section, "layout").c_str());
        #endif
        std::string layout = ini_reader->get(section, "layout");
        if (layout != "ui/infocomC.lyt") {
          // infocomC.lyt is the only lyt file that has a capital letter in the name
          std::transform(layout.begin(), layout.end(), layout.begin(), ::tolower);
        }
        this->layouts[section] = new UiLayout(ini_reader, resource_manager, cursor_manager, section);
      } else {
        // TODO: Implement support for ZTMapView, ZTMessageQueue and UIText here
        SDL_Log("Cannot load elements of type %s in layout manager yet, not implemented", type.c_str());
      }
    }

    // Increase progress
    if (*progress + progress_per_layout_load < 100.0f) {
      *progress = *progress + progress_per_layout_load;
    } else {
      *progress = 100.0f;
    }
  }
  this->loaded = true;
  delete ini_reader;
  this->loadScenarioList();
  this->loadFreeformMapList();
  this->updateStartingCashText();
  this->setupModScreen();
  *is_done = true;
}

// The ids of the scenario list, picture, story and objectives elements in
// ui/scenario.lyt
#define SCENARIO_LIST_ID 50002
#define SCENARIO_PICTURE_ID 50001
#define SCENARIO_STORY_ID 50004
#define SCENARIO_OBJECTIVES_ID 50006

void GameManager::loadScenarioList() {
  if (!this->layouts.contains("scenario")) {
    return;
  }
  UiListBox * list_box = dynamic_cast<UiListBox*>(this->layouts["scenario"]->getChildWithId(SCENARIO_LIST_ID));
  if (list_box == nullptr) {
    SDL_Log("Could not find the scenario list box");
    return;
  }

  // The scenario list is data driven: every cfg section with a scenario
  // key is one entry, carrying its display name id. The two letter section
  // names order the combined list across the base game, expansions and
  // updates: a* are the tutorials, b* to g* the difficulty groups. Using
  // the cfg name also shows scn02 as Tutorial 3 even though the scn file
  // claims the Tutorial 2 name.
  std::vector<std::string> config_files = this->resource_manager->getResourceNamesWithExtension("CFG");
  std::sort(config_files.begin(), config_files.end());

  // Section name, display name id and scenario file per entry
  std::vector<std::tuple<std::string, uint32_t, std::string>> entries;
  for (const std::string &config_file : config_files) {
    IniReader * config_reader = this->resource_manager->getIniReader(config_file);
    if (config_reader == nullptr) {
      continue;
    }
    for (std::string section : config_reader->getSections()) {
      std::string scenario_file = config_reader->get(section, "scenario");
      if (scenario_file.empty()) {
        continue;
      }
      entries.push_back({section, config_reader->getUnsignedInt(section, "name", 0), scenario_file});
    }
    delete config_reader;
  }
  std::sort(entries.begin(), entries.end());

  std::vector<std::string> scenario_names;
  this->scenarios.clear();
  for (const auto &[section, cfg_name_id, scenario_file] : entries) {
    IniReader * scenario_reader = this->resource_manager->getIniReader(scenario_file);
    if (scenario_reader == nullptr) {
      continue;
    }
    uint32_t name_id = cfg_name_id != 0 ? cfg_name_id : scenario_reader->getUnsignedInt("desc", "name", 0);
    std::string picture = scenario_reader->get("desc", "picture");
    delete scenario_reader;
    std::string scenario_name = this->resource_manager->getString(name_id);
    if (scenario_name.empty()) {
      continue;
    }
    scenario_names.push_back(scenario_name);
    // The story shown on the selection screen is the start.txt next to the
    // scenario file
    std::string story_file = "";
    size_t last_slash = scenario_file.rfind('/');
    if (last_slash != std::string::npos) {
      story_file = scenario_file.substr(0, last_slash + 1) + "start.txt";
    }
    this->scenarios.push_back({scenario_file, picture, story_file});
  }

  SDL_Log("Found %i scenarios", (int) scenario_names.size());
  list_box->setItems(scenario_names);
}

void GameManager::showSelectedScenario() {
  if (!this->layouts.contains("scenario")) {
    return;
  }
  UiLayout * layout = this->layouts["scenario"];
  UiListBox * list_box = dynamic_cast<UiListBox*>(layout->getChildWithId(SCENARIO_LIST_ID));
  if (list_box == nullptr) {
    return;
  }
  int selected_index = list_box->getSelectedIndex();
  if (selected_index < 0 || selected_index >= (int) this->scenarios.size()) {
    return;
  }
  ScenarioInfo scenario = this->scenarios[selected_index];

  UiImage * picture = dynamic_cast<UiImage*>(layout->getChildWithId(SCENARIO_PICTURE_ID));
  if (picture != nullptr && !scenario.picture.empty()) {
    picture->setImagePath(scenario.picture);
  }
  UiText * story = dynamic_cast<UiText*>(layout->getChildWithId(SCENARIO_STORY_ID));
  if (story != nullptr && !scenario.story_file.empty()) {
    story->setText(this->resource_manager->getTextFileContent(scenario.story_file));
  }

  UiListBox * objectives = dynamic_cast<UiListBox*>(layout->getChildWithId(SCENARIO_OBJECTIVES_ID));
  if (objectives != nullptr) {
    std::vector<std::string> objective_texts;
    IniReader * scenario_reader = this->resource_manager->getIniReader(scenario.file);
    if (scenario_reader != nullptr) {
      // A goal with display=1 in the duration section is shown as well
      if (scenario_reader->getInt("duration", "display", 0) == 1) {
        uint32_t text_id = scenario_reader->getUnsignedInt("duration", "text", 0);
        std::string objective_text = this->resource_manager->getString(text_id);
        if (!objective_text.empty()) {
          fillObjectivePlaceholder(objective_text, scenario_reader->getInt("duration", "nummonths", 0));
          objective_texts.push_back(objective_text);
        }
      }
      for (std::string goal : scenario_reader->getList("goals", "goal")) {
        uint32_t text_id = scenario_reader->getUnsignedInt(goal, "text", 0);
        if (text_id == 0) {
          continue;
        }
        std::string objective_text = this->resource_manager->getString(text_id);
        if (!objective_text.empty()) {
          fillObjectivePlaceholder(objective_text, scenario_reader->getInt(goal, "value", 0));
          objective_texts.push_back(objective_text);
        }
      }
      delete scenario_reader;
    }
    objectives->setItems(objective_texts);
  }
}

// Replaces the first %d in an objective text with the goal value. Texts with
// multiple placeholders keep the remaining ones, since it is not known yet
// which goal keys they refer to.
void GameManager::fillObjectivePlaceholder(std::string &text, int value) {
  size_t placeholder = text.find("%d");
  if (placeholder != std::string::npos) {
    text = text.substr(0, placeholder) + std::to_string(value) + text.substr(placeholder + 2);
  }
}

// The name of the freeform map selection layout in ui/gamescrn.lyt and the
// ids of its map list, map picture, map description, starting cash and
// spinner elements in ui/mapselec.lyt
#define FREEFORM_LAYOUT_NAME "load_maps_for_freeform"
#define MAP_LIST_ID 11504
#define MAP_PICTURE_ID 11501
#define MAP_DESCRIPTION_ID 11507
#define STARTING_CASH_ID 11510
#define CASH_UP_SPINNER_ID 11511
#define CASH_DOWN_SPINNER_ID 11512
#define FREEFORM_PLAY_ID 11513
#define DIFFICULTY_TEXT_ID 11531

// The difficulty strings in the lang dlls. Which starting cash amounts they
// correspond to is not in the game data. These thresholds were measured by
// stepping the original game's spinner through its whole range: hard up to
// $20,000, intermediate from $25,000, easy from $105,000.
#define DIFFICULTY_EASY_STRING_ID 11536
#define DIFFICULTY_INTERMEDIATE_STRING_ID 11537
#define DIFFICULTY_HARD_STRING_ID 11538
#define DIFFICULTY_EASY_MINIMUM_CASH 105000
#define DIFFICULTY_INTERMEDIATE_MINIMUM_CASH 25000

// The spinner default, step and limits come from the MS* keys in the UI
// section of zoo.ini. Like the original, the chosen amount persists while
// switching maps and screens.

void GameManager::loadFreeformMapList() {
  if (!this->layouts.contains(FREEFORM_LAYOUT_NAME)) {
    return;
  }
  UiListBox * list_box = dynamic_cast<UiListBox*>(this->layouts[FREEFORM_LAYOUT_NAME]->getChildWithId(MAP_LIST_ID));
  if (list_box == nullptr) {
    SDL_Log("Could not find the freeform map list box");
    return;
  }

  // The maps come from the freeform lists in the cfg files, in resource
  // name order: the expansion lists freefo01/freefo02 and then the base
  // game freeform.cfg. The original only shows the last 33 entries of this
  // sequence because they overflow a fixed size list, which hides the
  // Dinosaur Digs maps and most small Marine Mania maps; we show them all.
  std::vector<std::string> config_files = this->resource_manager->getResourceNamesWithExtension("CFG");
  std::sort(config_files.begin(), config_files.end());

  std::vector<std::string> map_files;
  for (const std::string &config_file : config_files) {
    IniReader * config_reader = this->resource_manager->getIniReader(config_file);
    if (config_reader == nullptr) {
      continue;
    }
    for (std::string map_file : config_reader->getList("freeform", "freeform")) {
      map_files.push_back(map_file);
    }
    delete config_reader;
  }

  std::vector<std::string> map_names;
  this->freeform_maps.clear();
  for (std::string scenario_file : map_files) {
    IniReader * scenario_reader = this->resource_manager->getIniReader(scenario_file);
    if (scenario_reader == nullptr) {
      continue;
    }
    uint32_t name_id = scenario_reader->getUnsignedInt("freeform", "name", 0);
    std::string icon = scenario_reader->get("freeform", "icon");
    std::string savegame = scenario_reader->get("start", "savegame");
    delete scenario_reader;
    std::string map_name = this->resource_manager->getString(name_id);
    if (map_name.empty()) {
      continue;
    }
    map_names.push_back(map_name);
    // The description shown on the selection screen is the txt file next to
    // the scenario file
    std::string description_file = scenario_file.substr(0, scenario_file.length() - 4) + ".txt";
    this->freeform_maps.push_back({icon, description_file, savegame});
  }

  SDL_Log("Found %i freeform maps", (int) map_names.size());
  list_box->setItems(map_names);
}

void GameManager::showSelectedFreeformMap() {
  if (!this->layouts.contains(FREEFORM_LAYOUT_NAME)) {
    return;
  }
  UiLayout * layout = this->layouts[FREEFORM_LAYOUT_NAME];
  UiListBox * list_box = dynamic_cast<UiListBox*>(layout->getChildWithId(MAP_LIST_ID));
  if (list_box == nullptr) {
    return;
  }
  int selected_index = list_box->getSelectedIndex();
  if (selected_index < 0 || selected_index >= (int) this->freeform_maps.size()) {
    return;
  }
  FreeformMapInfo map = this->freeform_maps[selected_index];

  UiImage * picture = dynamic_cast<UiImage*>(layout->getChildWithId(MAP_PICTURE_ID));
  if (picture != nullptr && !map.icon.empty()) {
    picture->setImagePath(map.icon);
  }
  UiText * description = dynamic_cast<UiText*>(layout->getChildWithId(MAP_DESCRIPTION_ID));
  if (description != nullptr && !map.description_file.empty()) {
    description->setText(this->resource_manager->getTextFileContent(map.description_file));
  }
  this->updateStartingCashText();
}

void GameManager::changeStartingCash(int amount) {
  Config * config = this->resource_manager->getConfig();
  this->starting_cash += amount;
  if (this->starting_cash < config->getFreeformCashMin()) {
    this->starting_cash = config->getFreeformCashMin();
  }
  if (this->starting_cash > config->getFreeformCashMax()) {
    this->starting_cash = config->getFreeformCashMax();
  }
  // The original persists the chosen amount in zoo.ini
  config->setFreeformStartingCash(this->starting_cash);
  this->updateStartingCashText();
}

void GameManager::updateStartingCashText() {
  if (!this->layouts.contains(FREEFORM_LAYOUT_NAME)) {
    return;
  }
  UiEditableText * cash_text = dynamic_cast<UiEditableText*>(this->layouts[FREEFORM_LAYOUT_NAME]->getChildWithId(STARTING_CASH_ID));
  if (cash_text == nullptr) {
    return;
  }
  cash_text->setText(this->formatMoney(this->starting_cash));

  // The difficulty follows from the chosen starting cash
  UiText * difficulty = dynamic_cast<UiText*>(this->layouts[FREEFORM_LAYOUT_NAME]->getChildWithId(DIFFICULTY_TEXT_ID));
  if (difficulty != nullptr) {
    uint32_t difficulty_string_id = DIFFICULTY_HARD_STRING_ID;
    if (this->starting_cash >= DIFFICULTY_EASY_MINIMUM_CASH) {
      difficulty_string_id = DIFFICULTY_EASY_STRING_ID;
    } else if (this->starting_cash >= DIFFICULTY_INTERMEDIATE_MINIMUM_CASH) {
      difficulty_string_id = DIFFICULTY_INTERMEDIATE_STRING_ID;
    }
    difficulty->setText(this->resource_manager->getString(difficulty_string_id));
  }
}

// The credits screen consists of multiple page layouts which the original
// game cycles through automatically. The page duration is not part of the
// layout data, so it is defined here.
#define CREDITS_PAGE_DURATION_MS 8000

void GameManager::updateCreditsPages() {
  if (!this->layouts.contains("credits")) {
    return;
  }
  UiLayout * credits = this->layouts["credits"];
  if (!credits->getActive()) {
    this->credits_active = false;
    return;
  }
  std::vector<UiLayout*> pages = credits->getChildLayouts();
  if (pages.empty()) {
    return;
  }

  uint64_t now = SDL_GetTicks();
  if (!this->credits_active) {
    // The credits screen was just opened, start at the first page
    this->credits_active = true;
    this->credits_page = 0;
    this->credits_page_start = now;
  } else if (now - this->credits_page_start >= CREDITS_PAGE_DURATION_MS) {
    this->credits_page = (this->credits_page + 1) % pages.size();
    this->credits_page_start = now;
  }

  for (size_t i = 0; i < pages.size(); i++) {
    pages[i]->setActive(i == this->credits_page);
  }
}

void GameManager::startFreeformMap() {
  if (!this->layouts.contains(FREEFORM_LAYOUT_NAME)) {
    return;
  }
  UiListBox * list_box = dynamic_cast<UiListBox*>(this->layouts[FREEFORM_LAYOUT_NAME]->getChildWithId(MAP_LIST_ID));
  if (list_box == nullptr) {
    return;
  }
  int selected_index = list_box->getSelectedIndex();
  if (selected_index < 0 || selected_index >= (int) this->freeform_maps.size()) {
    return;
  }
  std::string savegame = this->freeform_maps[selected_index].savegame;
  if (savegame.empty()) {
    return;
  }
  MapView * view = new MapView(this->resource_manager);
  if (!view->loadMap(savegame)) {
    delete view;
    return;
  }
  this->map_view = view;
  // A fresh game starts in January of year one with the chosen cash and,
  // like the original, paused
  this->simulation = new Simulation(1, this->starting_cash);
  // Until guests generate donations, the exhibits' saved monthly numbers
  // drive the money flow
  std::vector<ExhibitFinance> finances;
  for (const ZooExhibit &exhibit : view->getZoo()->getExhibits()) {
    ExhibitFinance finance;
    float donations = exhibit.last_donations > 0.0f ? exhibit.last_donations : exhibit.current_donations;
    float upkeep = exhibit.last_upkeep > 0.0f ? exhibit.last_upkeep : exhibit.current_upkeep;
    finance.monthly_donations_cents = (int64_t) (donations * 100.0f);
    finance.monthly_upkeep_cents = (int64_t) (upkeep * 100.0f);
    finances.push_back(finance);
  }
  this->simulation->setExhibitFinances(finances);
  this->simulation->setWorld(view->getZoo()->getEntranceX(), view->getZoo()->getEntranceY(), view->getPathTileKeys());
  // The animals come from the map records and the fences pen them in by
  // blocking tile edges
  std::vector<SimAnimal> animals;
  std::vector<uint64_t> blocked_edges;
  for (const ZooObject &object : view->getZoo()->getObjects()) {
    if (object.category == "animals" && (object.x != 0 || object.y != 0)) {
      SimAnimal animal;
      animal.x = (int32_t) object.x;
      animal.y = (int32_t) object.y;
      animal.target_x = animal.x;
      animal.target_y = animal.y;
      animal.wait_ticks = 0;
      animal.facing = (uint8_t) (object.rotation % 8);
      animal.species = object.subcategory;
      animal.sex = object.code;
      animals.push_back(animal);
    } else if (object.category == "fences" || object.category == "tankwall") {
      // Pieces sit on edge midpoints: a half tile x means the piece
      // blocks a step in y, and the other way around
      int32_t tile_x = (int32_t) (object.x / 64);
      int32_t tile_y = (int32_t) (object.y / 64);
      uint32_t fraction_x = object.x % 64;
      if (fraction_x >= 16 && fraction_x < 48) {
        int32_t edge_y = (int32_t) ((object.y + 32) / 64);
        blocked_edges.push_back(Simulation::makeEdgeKey(tile_x, edge_y - 1, 1));
      } else {
        int32_t edge_x = (int32_t) ((object.x + 32) / 64);
        blocked_edges.push_back(Simulation::makeEdgeKey(edge_x - 1, tile_y, 0));
      }
    }
  }
  std::sort(blocked_edges.begin(), blocked_edges.end());
  this->simulation->setBlockedEdges(blocked_edges);
  this->simulation->setAnimals(animals);
  this->simulation_paused = true;
  this->shown_month = -1;
  this->shown_year = -1;
  this->shown_cash = -1;
  this->setupGameHud();
}

void GameManager::leaveMap() {
  delete this->map_view;
  this->map_view = nullptr;
  if (this->simulation != nullptr) {
    delete this->simulation;
    this->simulation = nullptr;
  }
}

void GameManager::TickSimulation() {
  if (this->simulation != nullptr && this->map_view != nullptr && !this->simulation_paused) {
    this->simulation->tick();
  }
}


// Bring the toolbar into the state the original shows on a fresh zoo: a
// yellow-ish zoo rating, empty animal and guest meters until there are
// animals and guests, and no entity visibility toggles until they work
void GameManager::setupGameHud() {
  if (!this->layouts.contains("main")) {
    return;
  }
  UiLayout * layout = this->layouts["main"];
  UiStatusImage * zoo_status = dynamic_cast<UiStatusImage*>(layout->getChildWithId(HUD_ZOO_STATUS_ID));
  if (zoo_status != nullptr) {
    zoo_status->setValue(35);
  }
  for (int id : {HUD_ANIMAL_STATUS_ID, HUD_GUEST_STATUS_ID, HUD_TREE_TOGGLE_ID, HUD_BUILDING_TOGGLE_ID, HUD_GUEST_TOGGLE_ID}) {
    UiElement * element = layout->getChildWithId(id);
    if (element != nullptr) {
      element->setActive(false);
    }
  }
  ZtMiniMap * minimap = dynamic_cast<ZtMiniMap*>(layout->getChildWithId(HUD_MINIMAP_ID));
  if (minimap != nullptr && this->map_view != nullptr) {
    IniReader * minimap_colors = this->resource_manager->getIniReader("ui/miniclr.cfg");
    if (minimap_colors != nullptr) {
      minimap->setTiles(this->map_view->buildMinimapColors(minimap_colors),
                        (int) this->map_view->getMapWidth(), (int) this->map_view->getMapHeight());
      delete minimap_colors;
    }
    // The corner positions describe the map orientation to the widget
    SDL_FPoint origin;
    SDL_FPoint x_end;
    SDL_FPoint y_end;
    this->map_view->tileToUnit(0.0f, 0.0f, &origin.x, &origin.y);
    this->map_view->tileToUnit((float) this->map_view->getMapWidth(), 0.0f, &x_end.x, &x_end.y);
    this->map_view->tileToUnit(0.0f, (float) this->map_view->getMapHeight(), &y_end.x, &y_end.y);
    minimap->setTileTransform(origin, x_end, y_end);
  }
}

std::string GameManager::formatMoney(int64_t amount) {
  std::string cash_string = std::to_string(amount);
  for (int position = (int) cash_string.length() - 3; position > 0; position -= 3) {
    cash_string.insert(position, ",");
  }
  return "$" + cash_string;
}

void GameManager::updateGameHud(SDL_FRect * window_rect) {
  if (this->simulation == nullptr || !this->layouts.contains("main")) {
    return;
  }
  UiLayout * layout = this->layouts["main"];
  // The view outline on the minimap follows the camera
  ZtMiniMap * minimap = dynamic_cast<ZtMiniMap*>(layout->getChildWithId(HUD_MINIMAP_ID));
  if (minimap != nullptr && this->map_view != nullptr) {
    SDL_FPoint quad[4];
    const float corners[4][2] = {
      {0.0f, 0.0f}, {window_rect->w, 0.0f}, {window_rect->w, window_rect->h}, {0.0f, window_rect->h}};
    for (int i = 0; i < 4; i++) {
      this->map_view->screenToTile(corners[i][0], corners[i][1], window_rect, &quad[i].x, &quad[i].y);
    }
    minimap->setViewQuad(quad);
  }
  // The play button covers the pause button while paused. The pause
  // button stays active because the date display anchors to it.
  UiElement * play_button = layout->getChildWithId(HUD_PLAY_BUTTON_ID);
  if (play_button != nullptr) {
    play_button->setActive(this->simulation_paused);
  }
  int month = this->simulation->getMonth();
  int year = this->simulation->getYear();
  if (month != this->shown_month || year != this->shown_year) {
    UiText * date = dynamic_cast<UiText*>(layout->getChildWithId(HUD_DATE_ID));
    if (date != nullptr) {
      // The original formats the date as MMM, Year y
      std::string year_string = std::to_string(year);
      if (year_string.length() < 2) {
        year_string = "0" + year_string;
      }
      date->setText(this->resource_manager->getString(MONTH_STRING_ID_BASE + month) + ", Year " + year_string);
    }
    this->shown_month = month;
    this->shown_year = year;
  }
  int64_t cash = this->simulation->getCash();
  if (cash != this->shown_cash) {
    UiText * money = dynamic_cast<UiText*>(layout->getChildWithId(HUD_MONEY_ID));
    if (money != nullptr) {
      money->setText(this->formatMoney(cash));
    }
    this->shown_cash = cash;
  }
}

// The dead "Get New Zoo Tycoon Items" screen (ui/update.lyt, the online
// item service shut down long ago) hosts the mod manager. Its widgets are
// repurposed: the file list shows the mods with their enabled state and
// the buttons toggle and reorder them. Changes save immediately and apply
// on the next launch.
#define MOD_LAYOUT_NAME "update"
#define MOD_TITLE_ID 2101
#define MOD_INFO_LABEL_ID 2102
#define MOD_INFO_LIST_ID 2103
#define MOD_LIST_LABEL_ID 2105
#define MOD_LIST_ID 2106
#define MOD_DONE_BUTTON_ID 2110
#define MOD_STOP_BUTTON_ID 2111
#define MOD_TOGGLE_BUTTON_ID 2112
#define MOD_HIDDEN_BUTTON_ID 2115
#define MOD_MOVE_UP_BUTTON_ID 2114

void GameManager::setupModScreen() {
  if (!this->layouts.contains(MOD_LAYOUT_NAME)) {
    return;
  }
  UiLayout * layout = this->layouts[MOD_LAYOUT_NAME];
  UiText * title = dynamic_cast<UiText*>(layout->getChildWithId(MOD_TITLE_ID));
  if (title != nullptr) {
    title->setText("Zoo Tycoon Mods");
  }
  UiText * list_label = dynamic_cast<UiText*>(layout->getChildWithId(MOD_LIST_LABEL_ID));
  if (list_label != nullptr) {
    list_label->setText("Installed Mods");
  }
  UiText * info_label = dynamic_cast<UiText*>(layout->getChildWithId(MOD_INFO_LABEL_ID));
  if (info_label != nullptr) {
    info_label->setText("Information");
  }
  UiButton * done_button = dynamic_cast<UiButton*>(layout->getChildWithId(MOD_DONE_BUTTON_ID));
  if (done_button != nullptr) {
    done_button->setText("Done");
  }
  UiButton * toggle_button = dynamic_cast<UiButton*>(layout->getChildWithId(MOD_TOGGLE_BUTTON_ID));
  if (toggle_button != nullptr) {
    toggle_button->setText("Enable / Disable");
  }
  UiButton * move_button = dynamic_cast<UiButton*>(layout->getChildWithId(MOD_MOVE_UP_BUTTON_ID));
  if (move_button != nullptr) {
    move_button->setText("Move Up");
  }
  // The stop button overlaps the done button and the check items button
  // overlaps the toggle button, the original showed them one at a time
  UiElement * stop_button = layout->getChildWithId(MOD_STOP_BUTTON_ID);
  if (stop_button != nullptr) {
    stop_button->setActive(false);
  }
  UiElement * hidden_button = layout->getChildWithId(MOD_HIDDEN_BUTTON_ID);
  if (hidden_button != nullptr) {
    hidden_button->setActive(false);
  }
  this->refreshModList();
  this->showSelectedMod();
}

void GameManager::refreshModList() {
  if (!this->layouts.contains(MOD_LAYOUT_NAME)) {
    return;
  }
  UiListBox * list_box = dynamic_cast<UiListBox*>(this->layouts[MOD_LAYOUT_NAME]->getChildWithId(MOD_LIST_ID));
  if (list_box == nullptr) {
    return;
  }
  std::vector<std::string> items;
  if (this->mod_manager != nullptr) {
    for (const ModInfo &mod : this->mod_manager->getMods()) {
      items.push_back((mod.enabled ? "+ " : "-  ") + mod.file_name);
    }
  }
  if (items.empty()) {
    items.push_back("No mods found");
  }
  list_box->setItems(items);
}

void GameManager::showSelectedMod() {
  if (!this->layouts.contains(MOD_LAYOUT_NAME)) {
    return;
  }
  UiLayout * layout = this->layouts[MOD_LAYOUT_NAME];
  UiListBox * info = dynamic_cast<UiListBox*>(layout->getChildWithId(MOD_INFO_LIST_ID));
  UiListBox * list_box = dynamic_cast<UiListBox*>(layout->getChildWithId(MOD_LIST_ID));
  if (info == nullptr || list_box == nullptr) {
    return;
  }
  std::vector<std::string> lines;
  int selected_index = list_box->getSelectedIndex();
  if (this->mod_manager == nullptr || this->mod_manager->getMods().empty()) {
    lines.push_back("Put mod ztd files in the mods");
    lines.push_back("directory next to the game data");
  } else if (selected_index < 0 || selected_index >= (int) this->mod_manager->getMods().size()) {
    lines.push_back("Select a mod to manage it");
    lines.push_back("Earlier mods win conflicts");
  } else {
    const ModInfo &mod = this->mod_manager->getMods()[selected_index];
    lines.push_back(mod.file_name + (mod.enabled ? " is enabled" : " is disabled"));
    // Shared resource names with other enabled mods, the earlier mod wins
    std::vector<std::string> conflicts = this->mod_manager->getConflicts(selected_index);
    for (const std::string &other : conflicts) {
      int other_index = 0;
      for (const ModInfo &candidate : this->mod_manager->getMods()) {
        if (candidate.file_name == other) {
          break;
        }
        other_index++;
      }
      if (other_index < selected_index) {
        lines.push_back("Overridden by " + other);
      } else {
        lines.push_back("Overrides " + other);
      }
    }
    if (conflicts.empty()) {
      lines.push_back("Changes apply on the next launch");
    }
  }
  info->setItems(lines);
}

bool GameManager::handleTargetlessAction(UiAction action) {
  switch (action.source) {
    case (int) ActionSource::MAIN_MENU_EXIT:
      return false;
      break;
    case MAP_LIST_ID:
      this->showSelectedFreeformMap();
      break;
    case SCENARIO_LIST_ID:
      this->showSelectedScenario();
      break;
    case FREEFORM_PLAY_ID:
      this->startFreeformMap();
      break;
    case CASH_UP_SPINNER_ID:
      this->changeStartingCash(this->resource_manager->getConfig()->getFreeformCashIncrement());
      break;
    case CASH_DOWN_SPINNER_ID:
      this->changeStartingCash(-this->resource_manager->getConfig()->getFreeformCashIncrement());
      break;
    case MOD_LIST_ID:
      this->showSelectedMod();
      break;
    case MOD_TOGGLE_BUTTON_ID:
    case MOD_MOVE_UP_BUTTON_ID: {
      if (this->mod_manager == nullptr || !this->layouts.contains(MOD_LAYOUT_NAME)) {
        break;
      }
      UiListBox * list_box = dynamic_cast<UiListBox*>(this->layouts[MOD_LAYOUT_NAME]->getChildWithId(MOD_LIST_ID));
      if (list_box == nullptr) {
        break;
      }
      int selected_index = list_box->getSelectedIndex();
      if (selected_index < 0 || selected_index >= (int) this->mod_manager->getMods().size()) {
        break;
      }
      if (action.source == MOD_TOGGLE_BUTTON_ID) {
        this->mod_manager->toggle(selected_index);
      } else if (this->mod_manager->move(selected_index, -1)) {
        selected_index--;
      }
      this->refreshModList();
      list_box->setSelectedIndex(selected_index);
      this->showSelectedMod();
      break;
    }
    case MOD_DONE_BUTTON_ID:
      if (this->layouts.contains(MOD_LAYOUT_NAME)) {
        this->layouts[MOD_LAYOUT_NAME]->setActive(false);
      }
      break;
    default:
      break;
  }
  return true;
}
