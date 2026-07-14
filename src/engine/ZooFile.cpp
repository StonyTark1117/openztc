#include "ZooFile.hpp"

#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

static uint32_t readUint32(const uint8_t * data, size_t offset) {
  return (uint32_t) data[offset] | ((uint32_t) data[offset + 1] << 8) |
         ((uint32_t) data[offset + 2] << 16) | ((uint32_t) data[offset + 3] << 24);
}

static bool isAsciiString(const uint8_t * data, size_t size, size_t position, size_t &after) {
  if (position + 4 > size) {
    return false;
  }
  uint32_t string_length = readUint32(data, position);
  if (string_length < 1 || string_length > 32 || position + 4 + string_length > size) {
    return false;
  }
  for (uint32_t i = 0; i < string_length; i++) {
    uint8_t character = data[position + 4 + i];
    if (character < 0x20 || character >= 0x7f) {
      return false;
    }
  }
  after = position + 4 + string_length;
  return true;
}

// The object section starts with a count followed by records which begin
// with two length-prefixed ascii strings, the category and subcategory.
// Requiring both makes accidental matches inside the terrain stream
// practically impossible.
static bool isValidObjectAnchor(const uint8_t * data, size_t size, size_t position) {
  if (position + 4 > size) {
    return false;
  }
  uint32_t count = readUint32(data, position);
  if (count == 0) {
    // An empty object list is only believable at the very end of the data
    return size - position <= 8;
  }
  if (count > 100000) {
    return false;
  }
  size_t after_category = 0;
  if (!isAsciiString(data, size, position + 4, after_category)) {
    return false;
  }
  size_t after_subcategory = 0;
  return isAsciiString(data, size, after_category, after_subcategory);
}

ZooFile * ZooFile::loadFromMemory(const void * raw, size_t size) {
  const uint8_t * data = (const uint8_t *) raw;
  if (size < 0x30 || memcmp(data, "TZFB", 4) != 0) {
    SDL_Log("Not a zoo file, wrong magic");
    return nullptr;
  }

  char variant = (char) data[4];
  uint32_t language = readUint32(data, 8);

  // Variants F and G keep the dimensions right after the language id, the
  // later variants have an extra field in between
  uint32_t width = 0;
  uint32_t height = 0;
  size_t dims_offset = 0x0C;
  width = readUint32(data, dims_offset);
  height = readUint32(data, dims_offset + 4);
  if (width < 10 || width > 500 || width != height) {
    dims_offset = 0x10;
    width = readUint32(data, dims_offset);
    height = readUint32(data, dims_offset + 4);
  }
  if (width < 10 || width > 500 || width != height) {
    SDL_Log("Could not determine zoo map dimensions");
    return nullptr;
  }

  // Slide the terrain start until the end of the stream lands exactly on a
  // valid object section. Exhibit records of unknown size may sit between
  // the header and the terrain.
  size_t tile_count = (size_t) width * (size_t) height;
  size_t terrain_size = tile_count * 10;
  size_t terrain_start = 0;
  bool found = false;
  size_t search_end = size > terrain_size ? size - terrain_size : 0;
  if (search_end > 0x14 + 65536) {
    search_end = 0x14 + 65536;
  }
  for (size_t start = 0x14; start <= search_end; start++) {
    if (isValidObjectAnchor(data, size, start + terrain_size)) {
      terrain_start = start;
      found = true;
      break;
    }
  }
  if (!found) {
    SDL_Log("Could not locate the terrain stream in zoo file");
    return nullptr;
  }

  ZooFile * zoo = new ZooFile();
  zoo->variant = variant;
  zoo->language = language;
  zoo->width = width;
  zoo->height = height;
  zoo->tiles.reserve(tile_count);
  for (size_t i = 0; i < tile_count; i++) {
    size_t p = terrain_start + i * 10;
    ZooTerrainTile tile;
    tile.height = (int32_t) readUint32(data, p);
    tile.shape = data[p + 4];
    tile.type = data[p + 5];
    memcpy(tile.unknown, data + p + 6, 4);
    zoo->tiles.push_back(tile);
  }

  size_t object_start = terrain_start + terrain_size;
  zoo->object_count = readUint32(data, object_start);
  zoo->object_section.assign(data + object_start, data + size);
  zoo->parseObjects();
  return zoo;
}

// Object records are three length-prefixed strings, a length field for the
// rest of the record and that many bytes of data. The tile position in
// 64ths sits at a fixed place inside that data.
void ZooFile::parseObjects() {
  const uint8_t * data = this->object_section.data();
  size_t size = this->object_section.size();
  size_t position = 4;
  for (uint32_t i = 0; i < this->object_count; i++) {
    ZooObject object;
    std::string * strings[3] = {&object.category, &object.subcategory, &object.code};
    bool valid = true;
    for (int s = 0; s < 3 && valid; s++) {
      if (position + 4 > size) {
        valid = false;
        break;
      }
      uint32_t length = readUint32(data, position);
      if (length > 64 || position + 4 + length > size) {
        valid = false;
        break;
      }
      strings[s]->assign((const char *) data + position + 4, length);
      position += 4 + length;
    }
    if (!valid || position + 4 > size) {
      SDL_Log("Stopped parsing objects at %u of %u", i, this->object_count);
      break;
    }
    uint32_t remaining = readUint32(data, position);
    position += 4;
    if (position + remaining > size) {
      SDL_Log("Object %u data does not fit, stopping", i);
      break;
    }
    if (remaining >= 12) {
      object.x = readUint32(data, position + 4);
      object.y = readUint32(data, position + 8);
    } else {
      object.x = 0;
      object.y = 0;
    }
    position += remaining;
    this->objects.push_back(object);
  }
}

ZooFile * ZooFile::loadFromFile(const std::string &path) {
  FILE * fd = fopen(path.c_str(), "rb");
  if (fd == NULL) {
    SDL_Log("Could not open zoo file %s", path.c_str());
    return nullptr;
  }
  fseek(fd, 0L, SEEK_END);
  long size = ftell(fd);
  fseek(fd, 0L, SEEK_SET);
  if (size <= 0) {
    fclose(fd);
    return nullptr;
  }
  std::vector<uint8_t> buffer((size_t) size);
  size_t read_count = fread(buffer.data(), 1, (size_t) size, fd);
  fclose(fd);
  if (read_count != (size_t) size) {
    SDL_Log("Could not read zoo file %s", path.c_str());
    return nullptr;
  }
  return ZooFile::loadFromMemory(buffer.data(), buffer.size());
}

const ZooTerrainTile & ZooFile::getTile(uint32_t x, uint32_t y) const {
  return this->tiles[(size_t) y * this->width + x];
}
