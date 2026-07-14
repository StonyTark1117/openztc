#include "GameManager.hpp"

#include <string>
#include <algorithm>

#include "ui/UiListBox.hpp"
#include "ui/UiImage.hpp"
#include "ui/UiText.hpp"
#include "ui/UiEditableText.hpp"

GameManager::GameManager(ResourceManager * resource_manager, CursorManager * cursor_manager) {
  this->resource_manager = resource_manager;
  this->cursor_manager = cursor_manager;
}

GameManager::~GameManager() {
  for(auto kv : layouts) {
    delete kv.second;
    layouts[kv.first] = nullptr;
  }
}

bool GameManager::HandleInputs(std::vector<Input> &inputs) {
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

  std::vector<std::string> scenario_files = this->resource_manager->getResourceNamesWithExtension("SCN");
  std::sort(scenario_files.begin(), scenario_files.end());

  std::vector<std::string> scenario_names;
  this->scenarios.clear();
  for (std::string scenario_file : scenario_files) {
    IniReader * scenario_reader = this->resource_manager->getIniReader(scenario_file);
    if (scenario_reader == nullptr) {
      continue;
    }
    uint32_t name_id = scenario_reader->getUnsignedInt("desc", "name", 0);
    std::string picture = scenario_reader->get("desc", "picture");
    delete scenario_reader;
    if (name_id == 0) {
      // Scenarios without a name, like freeform.scn, are not shown in the list
      continue;
    }
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
          objective_texts.push_back(objective_text);
        }
      }
      delete scenario_reader;
    }
    objectives->setItems(objective_texts);
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

// The starting cash of scenario/freeform.scn, used when a map does not set
// its own. The step and limits used by the spinners are not in the game
// data, so they are defined here.
#define DEFAULT_STARTING_CASH 50000
#define STARTING_CASH_STEP 5000
#define STARTING_CASH_MIN 5000
#define STARTING_CASH_MAX 500000

void GameManager::loadFreeformMapList() {
  if (!this->layouts.contains(FREEFORM_LAYOUT_NAME)) {
    return;
  }
  UiListBox * list_box = dynamic_cast<UiListBox*>(this->layouts[FREEFORM_LAYOUT_NAME]->getChildWithId(MAP_LIST_ID));
  if (list_box == nullptr) {
    SDL_Log("Could not find the freeform map list box");
    return;
  }

  std::vector<std::string> scenario_files = this->resource_manager->getResourceNamesWithExtension("SCN");
  std::sort(scenario_files.begin(), scenario_files.end());

  std::vector<std::string> map_names;
  this->freeform_maps.clear();
  for (std::string scenario_file : scenario_files) {
    IniReader * scenario_reader = this->resource_manager->getIniReader(scenario_file);
    if (scenario_reader == nullptr) {
      continue;
    }
    uint32_t name_id = scenario_reader->getUnsignedInt("freeform", "name", 0);
    std::string icon = scenario_reader->get("freeform", "icon");
    int starting_cash = scenario_reader->getInt("start", "setcash", DEFAULT_STARTING_CASH);
    delete scenario_reader;
    if (name_id == 0) {
      // Only scenarios with a freeform section are maps
      continue;
    }
    std::string map_name = this->resource_manager->getString(name_id);
    if (map_name.empty()) {
      continue;
    }
    map_names.push_back(map_name);
    // The description shown on the selection screen is the txt file next to
    // the scenario file
    std::string description_file = scenario_file.substr(0, scenario_file.length() - 4) + ".txt";
    this->freeform_maps.push_back({icon, description_file, starting_cash});
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
  this->starting_cash = map.starting_cash;
  this->updateStartingCashText();
}

void GameManager::changeStartingCash(int amount) {
  this->starting_cash += amount;
  if (this->starting_cash < STARTING_CASH_MIN) {
    this->starting_cash = STARTING_CASH_MIN;
  }
  if (this->starting_cash > STARTING_CASH_MAX) {
    this->starting_cash = STARTING_CASH_MAX;
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
    case CASH_UP_SPINNER_ID:
      this->changeStartingCash(STARTING_CASH_STEP);
      break;
    case CASH_DOWN_SPINNER_ID:
      this->changeStartingCash(-STARTING_CASH_STEP);
      break;
    default:
      break;
  }
  return true;
}
