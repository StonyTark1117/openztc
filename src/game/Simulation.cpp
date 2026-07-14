#include "Simulation.hpp"

Simulation::Simulation(uint32_t seed, int64_t starting_cash) {
  // xorshift must not start at zero
  this->rng_state = seed == 0 ? 1 : seed;
  this->cash = starting_cash;
}

void Simulation::queueAction(const GameAction &action) {
  this->action_queue.push_back(action);
}

void Simulation::tick() {
  for (const GameAction &action : this->action_queue) {
    this->applyAction(action);
  }
  this->action_queue.clear();

  // Game state updates will happen here once there is game state
  this->tick_count++;
}

void Simulation::applyAction(const GameAction &action) {
  switch (action.type) {
    case GameActionType::DEBUG_NOOP:
      break;
    default:
      break;
  }
}

uint64_t Simulation::getTickCount() {
  return this->tick_count;
}

int Simulation::getMonth() {
  return (int) ((this->tick_count / TICKS_PER_MONTH) % 12);
}

int Simulation::getYear() {
  return (int) (this->tick_count / TICKS_PER_MONTH / 12) + 1;
}

int64_t Simulation::getCash() {
  return this->cash;
}

uint32_t Simulation::getChecksum() {
  return (uint32_t) this->tick_count ^ this->rng_state ^ (uint32_t) this->cash;
}

uint32_t Simulation::random(uint32_t bound) {
  // xorshift32, deterministic across platforms
  uint32_t x = this->rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  this->rng_state = x;
  if (bound == 0) {
    return 0;
  }
  return x % bound;
}
