#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "../src/engine/IniReader.hpp"

static IniReader readerFromString(const std::string &content) {
  return IniReader((void *) content.c_str(), content.size());
}

TEST_CASE("sections and keys are lowercased") {
  IniReader reader = readerFromString("[Section]\nKey = Value\n");
  CHECK(reader.get("section", "key", "") == "Value");
  CHECK(reader.get("SECTION", "KEY", "") == "Value");
}

TEST_CASE("missing keys return the default") {
  IniReader reader = readerFromString("[a]\nx = 1\n");
  CHECK(reader.get("a", "missing", "fallback") == "fallback");
  CHECK(reader.getInt("a", "missing", 7) == 7);
  CHECK(reader.getInt("a", "x", 7) == 1);
}

TEST_CASE("trailing whitespace in values is trimmed") {
  IniReader reader = readerFromString("[a]\ntype = UILayout \nother = x\t\n");
  CHECK(reader.get("a", "type", "") == "UILayout");
  CHECK(reader.get("a", "other", "") == "x");
}

TEST_CASE("repeated keys become a semicolon list") {
  IniReader reader = readerFromString("[c]\ncolor = 1\ncolor = 2\ncolor = 3\n");
  std::vector<int> values = reader.getIntList("c", "color");
  REQUIRE(values.size() == 3);
  CHECK(values[0] == 1);
  CHECK(values[1] == 2);
  CHECK(values[2] == 3);
}

TEST_CASE("comments and empty lines are ignored") {
  IniReader reader = readerFromString("; comment\n[a]\n; another\nx = 1\n\n");
  CHECK(reader.getInt("a", "x", 0) == 1);
}

TEST_CASE("windows line endings are handled") {
  IniReader reader = readerFromString("[a]\r\nx = hello\r\n");
  CHECK(reader.get("a", "x", "") == "hello");
}

TEST_CASE("sections can be listed") {
  IniReader reader = readerFromString("[one]\nx=1\n[two]\ny=2\n");
  std::vector<std::string> sections = reader.getSections();
  CHECK(sections.size() == 2);
}
