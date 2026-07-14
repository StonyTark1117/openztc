#ifndef SIMULATION_HPP
#define SIMULATION_HPP

#include <cstdint>
#include <vector>

#include "GameAction.hpp"

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
  // Game time: how many ticks make one game month.
  // TODO: calibrate against the original's clock speed
  static const int TICKS_PER_MONTH = 1200;

  Simulation(uint32_t seed = 0, int64_t starting_cash = 0);

  void queueAction(const GameAction &action);
  void tick();

  uint64_t getTickCount();
  // Game date derived from the tick count, month 0-11 and year from 1
  int getMonth();
  int getYear();
  int64_t getCash();
  // Cheap state fingerprint, used to detect desyncs when networking exists
  uint32_t getChecksum();
  // Deterministic pseudo random number in [0, bound)
  uint32_t random(uint32_t bound);

private:
  uint64_t tick_count = 0;
  uint32_t rng_state = 1;
  int64_t cash = 0;
  std::vector<GameAction> action_queue;

  void applyAction(const GameAction &action);
};

#endif // SIMULATION_HPP
