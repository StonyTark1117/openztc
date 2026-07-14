#ifndef GAME_ACTION_HPP
#define GAME_ACTION_HPP

#include <cstdint>
#include <string>
#include <vector>

// Every change to the game state is expressed as a GameAction and applied by
// the simulation at a tick boundary. The UI creates actions, it never
// changes game state directly. Keeping actions serializable is what makes
// replays and multiplayer possible later: a networked game is the same
// simulation applying the same actions on every peer.
enum class GameActionType : uint32_t {
  NONE = 0,
  // Placeholder until the first real actions (terrain, paths, fences) exist
  DEBUG_NOOP = 1,
};

typedef struct {
  GameActionType type = GameActionType::NONE;
  // Most actions target a tile
  int32_t x = 0;
  int32_t y = 0;
  // Generic parameters, meaning depends on the action type
  int32_t value = 0;
  std::string text = "";
} GameAction;

// Serialization keeps a stable little-endian layout so it can be used for
// replay files and network messages later
std::vector<uint8_t> serializeGameAction(const GameAction &action);
bool deserializeGameAction(const std::vector<uint8_t> &data, GameAction &action);

#endif // GAME_ACTION_HPP
