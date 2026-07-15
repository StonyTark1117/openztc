#include <doctest.h>

#include "../src/game/Simulation.hpp"
#include "../src/game/GameAction.hpp"

TEST_CASE("simulation ticks advance the counter") {
  Simulation simulation(42);
  CHECK(simulation.getTickCount() == 0);
  simulation.tick();
  simulation.tick();
  CHECK(simulation.getTickCount() == 2);
}

TEST_CASE("random is deterministic for the same seed") {
  Simulation a(1234);
  Simulation b(1234);
  for (int i = 0; i < 100; i++) {
    CHECK(a.random(1000) == b.random(1000));
  }
}

TEST_CASE("different seeds diverge") {
  Simulation a(1);
  Simulation b(2);
  bool any_different = false;
  for (int i = 0; i < 10; i++) {
    if (a.random(1000000) != b.random(1000000)) {
      any_different = true;
    }
  }
  CHECK(any_different);
}

TEST_CASE("game actions round trip through serialization") {
  GameAction action;
  action.type = GameActionType::DEBUG_NOOP;
  action.x = -5;
  action.y = 120;
  action.value = 987654;
  action.text = "Llama Exhibit";

  std::vector<uint8_t> data = serializeGameAction(action);
  GameAction restored;
  REQUIRE(deserializeGameAction(data, restored));
  CHECK(restored.type == action.type);
  CHECK(restored.x == action.x);
  CHECK(restored.y == action.y);
  CHECK(restored.value == action.value);
  CHECK(restored.text == action.text);
}

TEST_CASE("truncated action data is rejected") {
  GameAction action;
  action.type = GameActionType::DEBUG_NOOP;
  action.text = "hello";
  std::vector<uint8_t> data = serializeGameAction(action);
  data.resize(data.size() - 3);
  GameAction restored;
  CHECK(!deserializeGameAction(data, restored));
}

TEST_CASE("the date advances monthly from January of year one") {
  Simulation simulation(7, 75000);
  CHECK(simulation.getMonth() == 0);
  CHECK(simulation.getYear() == 1);
  CHECK(simulation.getCash() == 75000);
  for (int i = 0; i < Simulation::TICKS_PER_MONTH; i++) {
    simulation.tick();
  }
  CHECK(simulation.getMonth() == 1);
  CHECK(simulation.getYear() == 1);
  for (int i = 0; i < 11 * Simulation::TICKS_PER_MONTH; i++) {
    simulation.tick();
  }
  CHECK(simulation.getMonth() == 0);
  CHECK(simulation.getYear() == 2);
}

TEST_CASE("exhibit finances move the cash at month boundaries") {
  Simulation simulation(3, 1000);
  simulation.setExhibitFinances({{15050, 20000}, {0, 10025}});
  for (int i = 0; i < Simulation::TICKS_PER_MONTH - 1; i++) {
    simulation.tick();
  }
  CHECK(simulation.getCash() == 1000);
  simulation.tick();
  // 1000.00 + 150.50 - 200.00 - 100.25 = 850.25, displayed as whole dollars
  CHECK(simulation.getCash() == 850);
  for (int i = 0; i < Simulation::TICKS_PER_MONTH; i++) {
    simulation.tick();
  }
  CHECK(simulation.getCash() == 700);
}

TEST_CASE("identical simulations with finances stay in sync") {
  Simulation a(9, 500);
  Simulation b(9, 500);
  a.setExhibitFinances({{100, 250}});
  b.setExhibitFinances({{100, 250}});
  for (int i = 0; i < Simulation::TICKS_PER_MONTH * 2; i++) {
    a.tick();
    b.tick();
  }
  CHECK(a.getChecksum() == b.getChecksum());
}
