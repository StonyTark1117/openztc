// Developer tool: prints a summary of a zoo map/save file
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "../src/engine/ZooFile.hpp"

int main(int argc, char ** argv) {
  if (argc < 2) {
    printf("usage: dumpzoo [--objects] <file.zoo> [more.zoo ...]\n");
    return 1;
  }
  bool list_objects = false;
  bool roundtrip = false;
  int terrain_x = -1;
  int terrain_y = -1;
  int terrain_w = 0;
  int terrain_h = 0;
  int failures = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--objects") == 0) {
      list_objects = true;
      continue;
    }
    if (strcmp(argv[i], "--roundtrip") == 0) {
      roundtrip = true;
      continue;
    }
    // --terrain x y w h prints a height/shape/type grid for the following
    // files, for reverse engineering the shape bitfield
    if (strcmp(argv[i], "--terrain") == 0 && i + 4 < argc) {
      terrain_x = atoi(argv[i + 1]);
      terrain_y = atoi(argv[i + 2]);
      terrain_w = atoi(argv[i + 3]);
      terrain_h = atoi(argv[i + 4]);
      i += 4;
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
    if (terrain_x >= 0) {
      for (int ty = terrain_y; ty < terrain_y + terrain_h && ty < (int) zoo->getHeight(); ty++) {
        printf("  y=%3d ", ty);
        for (int tx = terrain_x; tx < terrain_x + terrain_w && tx < (int) zoo->getWidth(); tx++) {
          const ZooTerrainTile &tile = zoo->getTile(tx, ty);
          printf("%3d/%02x/%02x+%02x%02x%02x%02x ", tile.height, tile.shape, tile.type,
                 tile.edges, tile.padding[0], tile.padding[1], tile.padding[2]);
        }
        printf("\n");
      }
    }
    if (roundtrip) {
      FILE * fd = fopen(argv[i], "rb");
      fseek(fd, 0L, SEEK_END);
      long original_size = ftell(fd);
      fseek(fd, 0L, SEEK_SET);
      std::vector<uint8_t> original((size_t) original_size);
      size_t read_count = fread(original.data(), 1, (size_t) original_size, fd);
      fclose(fd);
      std::vector<uint8_t> written = zoo->serialize();
      if (read_count == written.size() && memcmp(original.data(), written.data(), written.size()) == 0) {
        printf("  roundtrip: byte identical\n");
      } else {
        printf("  roundtrip: MISMATCH (%zu vs %zu bytes)\n", read_count, written.size());
        failures++;
      }
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
