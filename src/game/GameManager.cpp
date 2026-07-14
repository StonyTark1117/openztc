#include "GameManager.hpp"

#include <string>
#include <algorithm>
#include <tuple>

#include "../ui/UiListBox.hpp"
#include "../ui/UiImage.hpp"
#include "../ui/UiText.hpp"
#include "../ui/UiEditableText.hpp"
#include "../ui/UiButton.hpp"

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
  for(auto kv : layouts) {
    delete kv.second;
    layouts[kv.first] = nullptr;
  }
}

bool GameManager::HandleInputs(std::vector<Input> &inputs) {
  if (this->map_view != nullptr) {
    if (!this->map_view->handleInputs(inputs)) {
      // Leaving the map returns to the menu
      delete this->map_view;
      this->map_view = nullptr;
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
    this->map_view->draw(renderer, window_rect);
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
  std::string cash_string = std::to_string(this->starting_cash);
  for (int position = (int) cash_string.length() - 3; position > 0; position -= 3) {
    cash_string.insert(position, ",");
  }
  cash_text->setText("$" + cash_string);

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
}

// The dead "Get New Zoo Tycoon Items" screen (ui/update.lyt, the online
// item service shut down long ago) hosts the mod manager. Its widgets are
// repurposed: the file list shows the mods with their enabled state and
// the buttons toggle and reorder them. Changes save immediately and apply
// on the next launch.
#define MOD_LAYOUT_NAME "Update"
#define MOD_TITLE_ID 2101
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
    lines.push_back("Changes apply on the next launch");
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
