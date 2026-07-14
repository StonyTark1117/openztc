#include "GameManager.hpp"

#include <string>
#include <algorithm>

GameManager::GameManager(ResourceManager * resource_manager) {
  this->resource_manager = resource_manager;
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
        this->layouts[section] = new UiLayout(ini_reader, resource_manager, section);
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
  *is_done = true;
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
    default:
      break;
  }
  return true;
}
