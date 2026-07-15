#include "Simulation.hpp"

#include <algorithm>

#include <SDL3/SDL.h>

Simulation::Simulation(uint32_t seed, int64_t starting_cash) {
  // xorshift must not start at zero
  this->rng_state = seed == 0 ? 1 : seed;
  this->cash_cents = starting_cash * 100;
}

void Simulation::setExhibitFinances(const std::vector<ExhibitFinance> &finances) {
  this->exhibit_finances = finances;
}

void Simulation::applyMonthlyFinances() {
  for (const ExhibitFinance &finance : this->exhibit_finances) {
    this->cash_cents += finance.monthly_donations_cents;
    this->cash_cents -= finance.monthly_upkeep_cents;
  }
}

void Simulation::queueAction(const GameAction &action) {
  this->action_queue.push_back(action);
}

void Simulation::tick() {
  for (const GameAction &action : this->action_queue) {
    this->applyAction(action);
  }
  this->action_queue.clear();

  this->updateGuests();
  this->updateAnimals();

  this->tick_count++;
  // Money moves at every month boundary
  if (this->tick_count % TICKS_PER_MONTH == 0) {
    this->applyMonthlyFinances();
  }
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
  return this->cash_cents / 100;
}

uint32_t Simulation::getChecksum() {
  uint32_t checksum = (uint32_t) this->tick_count ^ this->rng_state ^ (uint32_t) this->cash_cents;
  for (const SimGuest &guest : this->guests) {
    checksum = checksum * 31 + (uint32_t) guest.x;
    checksum = checksum * 31 + (uint32_t) guest.y;
  }
  for (const SimAnimal &animal : this->animals) {
    checksum = checksum * 31 + (uint32_t) animal.x;
    checksum = checksum * 31 + (uint32_t) animal.y;
  }
  return checksum;
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

bool Simulation::isPathTile(int32_t tile_x, int32_t tile_y) {
  if (tile_x < 0 || tile_y < 0) {
    return false;
  }
  uint64_t key = ((uint64_t) (uint32_t) tile_x << 32) | (uint32_t) tile_y;
  return std::binary_search(this->path_tiles.begin(), this->path_tiles.end(), key);
}

void Simulation::setWorld(int32_t entrance_tile_x, int32_t entrance_tile_y, std::vector<uint64_t> sorted_path_tiles) {
  this->path_tiles = sorted_path_tiles;
  this->spawn_tile_x = -1;
  this->spawn_tile_y = -1;
  // Guests appear on the path tile closest to the entrance. The search
  // order is fixed so every peer finds the same tile.
  for (int32_t radius = 0; radius < 8 && this->spawn_tile_x < 0; radius++) {
    for (int32_t dy = -radius; dy <= radius && this->spawn_tile_x < 0; dy++) {
      for (int32_t dx = -radius; dx <= radius && this->spawn_tile_x < 0; dx++) {
        if (this->isPathTile(entrance_tile_x + dx, entrance_tile_y + dy)) {
          this->spawn_tile_x = entrance_tile_x + dx;
          this->spawn_tile_y = entrance_tile_y + dy;
        }
      }
    }
  }
}

// How often a new guest walks in, how many at most, and the walk speed
#define GUEST_SPAWN_INTERVAL_TICKS 400
#define GUEST_LIMIT 30
#define GUEST_SPEED_PER_TICK 2

void Simulation::spawnGuest() {
  SimGuest guest;
  guest.x = this->spawn_tile_x * 64 + 32;
  guest.y = this->spawn_tile_y * 64 + 32;
  guest.target_x = guest.x;
  guest.target_y = guest.y;
  guest.previous_tile_x = this->spawn_tile_x;
  guest.previous_tile_y = this->spawn_tile_y;
  guest.type = (uint8_t) this->random(4);
  guest.facing = 0;
  this->guests.push_back(guest);
}

void Simulation::updateGuests() {
  for (SimGuest &guest : this->guests) {
    if (guest.x == guest.target_x && guest.y == guest.target_y) {
      // Pick the next path tile, avoiding an immediate turn back unless
      // the path dead ends
      int32_t tile_x = guest.x / 64;
      int32_t tile_y = guest.y / 64;
      const int32_t neighbors[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
      int32_t options[4][2];
      int option_count = 0;
      int back_index = -1;
      for (int i = 0; i < 4; i++) {
        int32_t next_x = tile_x + neighbors[i][0];
        int32_t next_y = tile_y + neighbors[i][1];
        if (!this->isPathTile(next_x, next_y)) {
          continue;
        }
        if (next_x == guest.previous_tile_x && next_y == guest.previous_tile_y) {
          back_index = option_count;
        }
        options[option_count][0] = next_x;
        options[option_count][1] = next_y;
        option_count++;
      }
      if (option_count == 0) {
        continue;
      }
      int choice = (int) this->random((uint32_t) option_count);
      if (option_count > 1 && choice == back_index) {
        // Prefer moving on over turning around
        choice = (choice + 1) % option_count;
      }
      guest.previous_tile_x = tile_x;
      guest.previous_tile_y = tile_y;
      guest.target_x = options[choice][0] * 64 + 32;
      guest.target_y = options[choice][1] * 64 + 32;
    }
    // Walk toward the target, one axis at a time since path steps are
    // axis aligned. The facing follows the movement, clockwise from NW
    // like the save format stores it: 2 faces +x, 4 +y, 6 -x, 0 -y.
    if (guest.x < guest.target_x) {
      guest.x += SDL_min(GUEST_SPEED_PER_TICK, guest.target_x - guest.x);
      guest.facing = 2;
    } else if (guest.x > guest.target_x) {
      guest.x -= SDL_min(GUEST_SPEED_PER_TICK, guest.x - guest.target_x);
      guest.facing = 6;
    } else if (guest.y < guest.target_y) {
      guest.y += SDL_min(GUEST_SPEED_PER_TICK, guest.target_y - guest.y);
      guest.facing = 4;
    } else if (guest.y > guest.target_y) {
      guest.y -= SDL_min(GUEST_SPEED_PER_TICK, guest.y - guest.target_y);
      guest.facing = 0;
    }
  }
  if (this->spawn_tile_x >= 0 && this->guests.size() < GUEST_LIMIT &&
      this->tick_count % GUEST_SPAWN_INTERVAL_TICKS == 0) {
    this->spawnGuest();
  }
}

void Simulation::setAnimals(const std::vector<SimAnimal> &animals) {
  this->animals = animals;
}

void Simulation::setBlockedEdges(std::vector<uint64_t> sorted_edges) {
  this->blocked_edges = sorted_edges;
}

bool Simulation::isEdgeBlocked(int32_t tile_x, int32_t tile_y, int32_t next_x, int32_t next_y) {
  uint64_t key;
  if (next_x > tile_x) {
    key = makeEdgeKey(tile_x, tile_y, 0);
  } else if (next_x < tile_x) {
    key = makeEdgeKey(next_x, next_y, 0);
  } else if (next_y > tile_y) {
    key = makeEdgeKey(tile_x, tile_y, 1);
  } else {
    key = makeEdgeKey(next_x, next_y, 1);
  }
  return std::binary_search(this->blocked_edges.begin(), this->blocked_edges.end(), key);
}

// Animals amble slower than guests and rest between moves
#define ANIMAL_SPEED_PER_TICK 1

void Simulation::updateAnimals() {
  for (SimAnimal &animal : this->animals) {
    if (animal.wait_ticks > 0) {
      animal.wait_ticks--;
      continue;
    }
    if (animal.x == animal.target_x && animal.y == animal.target_y) {
      // Rest a bit, then amble to a neighbor tile no fence blocks
      animal.wait_ticks = (int32_t) this->random(160) + 40;
      int32_t tile_x = animal.x / 64;
      int32_t tile_y = animal.y / 64;
      const int32_t neighbors[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
      int32_t options[4][2];
      int option_count = 0;
      for (int i = 0; i < 4; i++) {
        int32_t next_x = tile_x + neighbors[i][0];
        int32_t next_y = tile_y + neighbors[i][1];
        if (next_x < 0 || next_y < 0 || this->isEdgeBlocked(tile_x, tile_y, next_x, next_y)) {
          continue;
        }
        options[option_count][0] = next_x;
        options[option_count][1] = next_y;
        option_count++;
      }
      if (option_count == 0) {
        continue;
      }
      int choice = (int) this->random((uint32_t) option_count);
      animal.target_x = options[choice][0] * 64 + 32;
      animal.target_y = options[choice][1] * 64 + 32;
      continue;
    }
    if (animal.x < animal.target_x) {
      animal.x += SDL_min(ANIMAL_SPEED_PER_TICK, animal.target_x - animal.x);
      animal.facing = 2;
    } else if (animal.x > animal.target_x) {
      animal.x -= SDL_min(ANIMAL_SPEED_PER_TICK, animal.x - animal.target_x);
      animal.facing = 6;
    } else if (animal.y < animal.target_y) {
      animal.y += SDL_min(ANIMAL_SPEED_PER_TICK, animal.target_y - animal.y);
      animal.facing = 4;
    } else if (animal.y > animal.target_y) {
      animal.y -= SDL_min(ANIMAL_SPEED_PER_TICK, animal.y - animal.target_y);
      animal.facing = 0;
    }
  }
}
