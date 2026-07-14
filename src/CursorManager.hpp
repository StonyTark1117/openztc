#ifndef CURSOR_MANAGER_HPP
#define CURSOR_MANAGER_HPP

#include <unordered_map>
#include <cstdint>

#include <SDL3/SDL.h>

#include "ResourceManager.hpp"

// Cursor resource ids found in res0.dll. Ids 9 to 20 are the color cursors,
// ids 21 to 32 are monochrome variants of the same set.
enum class CursorRole {
  NONE = 0,       // No cursor loaded for this role
  DEFAULT = 9,    // Arrow
  TEXT = 10,      // Text I-beam
  FENCE = 11,     // Tile with fence pieces
  CONSTRUCTION = 12, // Arrow with construction vehicle
  ELEVATION = 13, // Up/down arrows (raise/lower terrain)
  MOVE = 14,      // Four-way move arrows
  SHOVEL = 15,    // Shovel (terrain painting)
  BULLDOZER = 16, // Arrow with bulldozer (demolish)
  GRAB = 17,      // Grabbing hand
  HAND = 18,      // Open hand
  HOVER = 19,     // Pointing hand
  WAIT = 20,      // Hourglass
};

class CursorManager {
public:
  CursorManager(ResourceManager * resource_manager);
  ~CursorManager();

  void setCursor(CursorRole role);

private:
  ResourceManager * resource_manager = nullptr;
  std::unordered_map<CursorRole, SDL_Cursor *> cursor_map;
  CursorRole current_role = CursorRole::NONE;

  SDL_Cursor * getCursor(CursorRole role);
};

#endif // CURSOR_MANAGER_HPP
