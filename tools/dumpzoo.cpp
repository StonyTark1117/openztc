// Developer tool: prints a summary of a zoo map/save file
#include <cstdio>
#include <map>

#include "../src/engine/ZooFile.hpp"

int main(int argc, char ** argv) {
  if (argc < 2) {
    printf("usage: dumpzoo <file.zoo> [more.zoo ...]\n");
    return 1;
  }
  int failures = 0;
  for (int i = 1; i < argc; i++) {
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
    printf("\n");
    delete zoo;
  }
  return failures == 0 ? 0 : 1;
}
