#ifndef SIMULATION_HPP
#define SIMULATION_HPP

#include <cstdint>
#include <vector>

#include "GameAction.hpp"

// Monthly money flow of one exhibit in cents, taken from the loaded map
// until guests generate donations themselves
typedef struct {
  int64_t monthly_donations_cents;
  int64_t monthly_upkeep_cents;
} ExhibitFinance;

// A guest walking the zoo. Positions are in 64ths of a tile, like the
// save format uses, and stay integers for determinism.
typedef struct {
  int32_t x;
  int32_t y;
  int32_t target_x;
  int32_t target_y;
  int32_t previous_tile_x;
  int32_t previous_tile_y;
  uint8_t type;    // 0 man, 1 woman, 2 boy, 3 girl
  uint8_t facing;  // 0-7 clockwise from SE
} SimGuest;

// The deterministic game simulation. It advances in fixed ticks, consumes
// queued GameActions at tick boundaries and owns the only random number
// generator game logic may use. Determinism rules for everything inside the
// simulation:
// - integer math where possible, no floats in game state
// - randomness only through random(), never external state
// - no iteration order dependence on containers with unspecified order
// These rules are what keep replays and future multiplayer in sync.
class Simulation {
public:
  // Fixed number of simulation ticks per second. The renderer runs
  // independently of this.
  static const int TICKS_PER_SECOND = 20;
  // Game time: one month is five real minutes in the original, so a year
  // is one hour. Measured against the original under Wine and documented
  // by the community wiki.
  static const int TICKS_PER_MONTH = 300 * TICKS_PER_SECOND;

  Simulation(uint32_t seed = 0, int64_t starting_cash = 0);

  void queueAction(const GameAction &action);
  void tick();
  // The exhibits' monthly donations and upkeep, applied at month ticks
  void setExhibitFinances(const std::vector<ExhibitFinance> &finances);
  // The walkable world: the entrance tile and the path tiles as sorted
  // (x << 32 | y) keys. Guests spawn near the entrance and wander the
  // paths once this is set.
  void setWorld(int32_t entrance_tile_x, int32_t entrance_tile_y, std::vector<uint64_t> sorted_path_tiles);
  const std::vector<SimGuest> & getGuests() { return this->guests; }

  uint64_t getTickCount();
  // Game date derived from the tick count, month 0-11 and year from 1
  int getMonth();
  int getYear();
  // Whole dollars for display, the internal bookkeeping is in cents
  int64_t getCash();
  // Cheap state fingerprint, used to detect desyncs when networking exists
  uint32_t getChecksum();
  // Deterministic pseudo random number in [0, bound)
  uint32_t random(uint32_t bound);

private:
  uint64_t tick_count = 0;
  uint32_t rng_state = 1;
  int64_t cash_cents = 0;
  std::vector<ExhibitFinance> exhibit_finances;
  std::vector<GameAction> action_queue;
  std::vector<uint64_t> path_tiles;
  std::vector<SimGuest> guests;
  int32_t spawn_tile_x = -1;
  int32_t spawn_tile_y = -1;

  void applyAction(const GameAction &action);
  void applyMonthlyFinances();
  bool isPathTile(int32_t tile_x, int32_t tile_y);
  void updateGuests();
  void spawnGuest();
};

#endif // SIMULATION_HPP
