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
