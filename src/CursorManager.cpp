#include "CursorManager.hpp"

CursorManager::CursorManager(ResourceManager * resource_manager) {
  this->resource_manager = resource_manager;
}

CursorManager::~CursorManager() {
  for (auto kv : this->cursor_map) {
    SDL_DestroyCursor(kv.second);
  }
}

void CursorManager::setCursor(CursorRole role) {
  if (role == this->current_role) {
    return;
  }
  SDL_Cursor * cursor = this->getCursor(role);
  if (cursor != nullptr) {
    SDL_SetCursor(cursor);
    this->current_role = role;
  }
}

SDL_Cursor * CursorManager::getCursor(CursorRole role) {
  if (this->cursor_map.contains(role)) {
    return this->cursor_map[role];
  }
  SDL_Cursor * cursor = this->resource_manager->getCursor((uint32_t) role);
  if (cursor == nullptr) {
    SDL_Log("Could not load cursor for role %u", (uint32_t) role);
    return nullptr;
  }
  this->cursor_map[role] = cursor;
  return cursor;
}
