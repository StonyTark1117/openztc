// Developer tool: prints a summary of a zoo map/save file
#include <cstdio>
#include <cstring>
#include <map>

#include "../src/engine/ZooFile.hpp"

int main(int argc, char ** argv) {
  if (argc < 2) {
    printf("usage: dumpzoo [--objects] <file.zoo> [more.zoo ...]\n");
    return 1;
  }
  bool list_objects = false;
  int failures = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--objects") == 0) {
      list_objects = true;
      continue;
    }
    ZooFile * zoo = ZooFile::loadFromFile(argv[i]);
    if (zoo == nullptr) {
      printf("%s: FAILED to parse\n", argv[i]);
      failures++;
      continue;
    }
    std::map<uint8_t, int> type_counts;
    int32_t max_height = 0;
    int32_t min_height = 0;
    for (uint32_t y = 0; y < zoo->getHeight(); y++) {
      for (uint32_t x = 0; x < zoo->getWidth(); x++) {
        const ZooTerrainTile &tile = zoo->getTile(x, y);
        type_counts[tile.type]++;
        if (tile.height > max_height) {
          max_height = tile.height;
        }
        if (tile.height < min_height) {
          min_height = tile.height;
        }
      }
    }
    printf("%s: variant=%c lang=%u size=%ux%u objects=%u heights=[%d..%d] terrain_types=",
           argv[i], zoo->getVariant(), zoo->getLanguage(), zoo->getWidth(),
           zoo->getHeight(), zoo->getObjectCount(), min_height, max_height);
    for (auto entry : type_counts) {
      printf("%02x:%d ", entry.first, entry.second);
    }
    printf("  entrance=%d,%d exhibits=%u\n", zoo->getEntranceX(), zoo->getEntranceY(), (uint32_t) zoo->getExhibits().size());
    for (const ZooExhibit &exhibit : zoo->getExhibits()) {
      printf("  exhibit \"%s\" at %d,%d entrance %d,%d rot %d donations %.2f/%.2f/%.2f upkeep %.2f/%.2f/%.2f ext %x\n",
             exhibit.name.c_str(), exhibit.x, exhibit.y, exhibit.entrance_x, exhibit.entrance_y,
             exhibit.entrance_rotation, exhibit.current_donations, exhibit.last_donations, exhibit.total_donations,
             exhibit.current_upkeep, exhibit.last_upkeep, exhibit.total_upkeep, exhibit.extension_type);
    }
    if (list_objects) {
      for (const ZooObject &object : zoo->getObjects()) {
        printf("  %s/%s/%s x=%u (%.2f) y=%u (%.2f) rotation=%u\n",
               object.category.c_str(), object.subcategory.c_str(), object.code.c_str(),
               object.x, (float) object.x / 64.0f, object.y, (float) object.y / 64.0f, object.rotation);
      }
    }
    delete zoo;
  }
  return failures == 0 ? 0 : 1;
}
