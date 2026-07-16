#include <doctest.h>

#include <cstring>
#include <vector>

#include "../src/engine/ZooFile.hpp"

// Builds a minimal synthetic zoo file: F-variant header, a WxW terrain
// stream and an object section with one record
static std::vector<uint8_t> syntheticZoo(uint32_t size, size_t header_padding) {
  std::vector<uint8_t> data;
  auto push32 = [&data](uint32_t value) {
    data.push_back(value & 0xFF);
    data.push_back((value >> 8) & 0xFF);
    data.push_back((value >> 16) & 0xFF);
    data.push_back((value >> 24) & 0xFF);
  };
  const char * magic = "TZFBF";
  data.insert(data.end(), magic, magic + 5);
  data.push_back(0);
  data.push_back(0);
  data.push_back(0);
  push32(1033);       // language
  push32(size);       // width
  push32(size);       // height
  // variable header remainder, like the real variants have
  for (size_t i = 0; i < header_padding; i++) {
    data.push_back(0);
  }
  // terrain stream
  for (uint32_t i = 0; i < size * size; i++) {
    push32(i % 5);          // height
    data.push_back(0x0F);   // shape
    data.push_back(0x03);   // type
    push32(0);              // unknown
  }
  // object section: one record starting with category and subcategory
  push32(1);
  push32(5);
  const char * category = "paths";
  data.insert(data.end(), category, category + 5);
  push32(5);
  data.insert(data.end(), category, category + 5);
  return data;
}

TEST_CASE("synthetic zoo file parses") {
  std::vector<uint8_t> file = syntheticZoo(20, 12);
  ZooFile * zoo = ZooFile::loadFromMemory(file.data(), file.size());
  REQUIRE(zoo != nullptr);
  CHECK(zoo->getWidth() == 20);
  CHECK(zoo->getHeight() == 20);
  CHECK(zoo->getVariant() == 'F');
  CHECK(zoo->getLanguage() == 1033);
  CHECK(zoo->getObjectCount() == 1);
  CHECK(zoo->getTile(0, 0).height == 0);
  CHECK(zoo->getTile(1, 0).height == 1);
  CHECK(zoo->getTile(0, 0).type == 0x03);
  CHECK(zoo->getTile(0, 0).shape == 0x0F);
  delete zoo;
}

TEST_CASE("header size differences are absorbed by the anchor search") {
  for (size_t padding : {0, 2, 12, 40, 300}) {
    std::vector<uint8_t> file = syntheticZoo(15, padding);
    ZooFile * zoo = ZooFile::loadFromMemory(file.data(), file.size());
    REQUIRE(zoo != nullptr);
    CHECK(zoo->getWidth() == 15);
    CHECK(zoo->getTile(14, 14).type == 0x03);
    delete zoo;
  }
}

TEST_CASE("garbage is rejected") {
  std::vector<uint8_t> garbage(1000, 0xAB);
  CHECK(ZooFile::loadFromMemory(garbage.data(), garbage.size()) == nullptr);
  std::vector<uint8_t> tiny = {'T', 'Z', 'F', 'B'};
  CHECK(ZooFile::loadFromMemory(tiny.data(), tiny.size()) == nullptr);
}

TEST_CASE("truncated terrain is rejected") {
  std::vector<uint8_t> file = syntheticZoo(20, 12);
  file.resize(file.size() / 2);
  CHECK(ZooFile::loadFromMemory(file.data(), file.size()) == nullptr);
}

// Builds an F-variant zoo with an entrance and one exhibit before the
// terrain, following the layout the shipped maps use
static std::vector<uint8_t> syntheticZooWithExhibit() {
  std::vector<uint8_t> data;
  auto push32 = [&data](uint32_t value) {
    data.push_back(value & 0xFF);
    data.push_back((value >> 8) & 0xFF);
    data.push_back((value >> 16) & 0xFF);
    data.push_back((value >> 24) & 0xFF);
  };
  auto pushFloat = [&data, &push32](float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    push32(bits);
  };
  const char * magic = "TZFBF";
  data.insert(data.end(), magic, magic + 5);
  data.push_back(0);
  data.push_back(0);
  data.push_back(0);
  push32(1033);   // language
  push32(20);     // width
  push32(20);     // height
  push32(5);      // entrance x
  push32(9);      // entrance y
  push32(1);      // exhibit count
  push32(3);      // exhibit x
  push32(4);      // exhibit y
  const char * name = "Lion Land";
  push32(9);
  data.insert(data.end(), name, name + 9);
  push32(6);      // entrance x
  push32(7);      // entrance y
  push32(2);      // entrance rotation
  pushFloat(1.5f);   // current donations
  pushFloat(2.5f);   // last donations
  pushFloat(3.5f);   // total donations
  pushFloat(4.5f);   // current upkeep
  pushFloat(5.5f);   // last upkeep
  pushFloat(6.5f);   // total upkeep
  for (int i = 0; i < 28; i++) {
    data.push_back(0);  // the F variant record tail
  }
  push32(0);      // the two undocumented values before the terrain
  push32(0);
  for (uint32_t i = 0; i < 20 * 20; i++) {
    push32(0);
    data.push_back(0x0F);
    data.push_back(0x03);
    push32(0);
  }
  // empty object section
  push32(0);
  return data;
}

TEST_CASE("exhibits and the entrance parse from the header") {
  std::vector<uint8_t> file = syntheticZooWithExhibit();
  ZooFile * zoo = ZooFile::loadFromMemory(file.data(), file.size());
  REQUIRE(zoo != nullptr);
  CHECK(zoo->getEntranceX() == 5);
  CHECK(zoo->getEntranceY() == 9);
  REQUIRE(zoo->getExhibits().size() == 1);
  const ZooExhibit &exhibit = zoo->getExhibits()[0];
  CHECK(exhibit.name == "Lion Land");
  CHECK(exhibit.x == 3);
  CHECK(exhibit.y == 4);
  CHECK(exhibit.entrance_x == 6);
  CHECK(exhibit.entrance_y == 7);
  CHECK(exhibit.entrance_rotation == 2);
  CHECK(exhibit.total_donations == doctest::Approx(3.5f));
  CHECK(exhibit.total_upkeep == doctest::Approx(6.5f));
  delete zoo;
}

TEST_CASE("a loaded zoo serializes back byte identical") {
  std::vector<uint8_t> file = syntheticZooWithExhibit();
  ZooFile * zoo = ZooFile::loadFromMemory(file.data(), file.size());
  REQUIRE(zoo != nullptr);
  std::vector<uint8_t> written = zoo->serialize();
  REQUIRE(written.size() == file.size());
  CHECK(memcmp(written.data(), file.data(), file.size()) == 0);
  delete zoo;
}

// A building record shaped like the real ones: three length-prefixed
// strings, a length field, then x/y/elevation/rotation, an id, the
// length-prefixed display name and the state that carries the color
// choice 43 bytes in
static std::vector<uint8_t> syntheticZooWithBuilding(uint8_t color, size_t state_bytes) {
  std::vector<uint8_t> data = syntheticZoo(10, 12);
  // drop the one-record object section syntheticZoo appended
  data.resize(data.size() - (4 + 4 + 5 + 4 + 5));
  auto push32 = [&data](uint32_t value) {
    data.push_back(value & 0xFF);
    data.push_back((value >> 8) & 0xFF);
    data.push_back((value >> 16) & 0xFF);
    data.push_back((value >> 24) & 0xFF);
  };
  auto pushString = [&data, &push32](const char * text) {
    uint32_t length = (uint32_t) strlen(text);
    push32(length);
    data.insert(data.end(), text, text + length);
  };
  push32(1);
  pushString("building");
  pushString("building");
  pushString("hdogstnd");
  const char * name = "Hot Dog Stand 1";
  uint32_t name_length = (uint32_t) strlen(name);
  uint32_t remaining = 28 + name_length + (uint32_t) state_bytes;
  push32(remaining);
  push32(0);            // leading field
  push32(5 * 64);       // x
  push32(6 * 64);       // y
  push32((uint32_t) -16);  // elevation
  push32(4);            // rotation
  push32(0x1234);       // id
  pushString(name);
  for (size_t i = 0; i < state_bytes; i++) {
    data.push_back(i == 43 ? color : 0);
  }
  return data;
}

TEST_CASE("a building's color choice parses from its record") {
  std::vector<uint8_t> file = syntheticZooWithBuilding(22, 104);
  ZooFile * zoo = ZooFile::loadFromMemory(file.data(), file.size());
  REQUIRE(zoo != nullptr);
  REQUIRE(zoo->getObjects().size() == 1);
  const ZooObject &object = zoo->getObjects()[0];
  CHECK(object.code == "hdogstnd");
  CHECK(object.x == 5 * 64);
  CHECK(object.elevation == -16);
  CHECK(object.color == 22);
  delete zoo;
}

TEST_CASE("a record too short for the color stays unknown") {
  std::vector<uint8_t> file = syntheticZooWithBuilding(22, 20);
  ZooFile * zoo = ZooFile::loadFromMemory(file.data(), file.size());
  REQUIRE(zoo != nullptr);
  REQUIRE(zoo->getObjects().size() == 1);
  CHECK(zoo->getObjects()[0].color == 255);
  delete zoo;
}
