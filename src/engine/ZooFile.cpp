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

static float readFloat(const uint8_t * data, size_t position) {
  float value;
  memcpy(&value, data + position, 4);
  return value;
}

// The exhibit list follows the entrance position in the header: a count,
// then per exhibit the southern corner, a length-prefixed name, the
// entrance tile and rotation, six running money numbers, 30 undocumented
// bytes and an extension type with extra data for tanks. Returns the
// position after the list, or 0 when the data does not look like exhibits,
// in which case the caller falls back to sliding the terrain start.
size_t ZooFile::parseExhibits(const uint8_t * data, size_t size, size_t position, char variant, std::vector<ZooExhibit> &exhibits) {
  if (position + 4 > size) {
    return 0;
  }
  uint32_t count = readUint32(data, position);
  position += 4;
  if (count > 1000) {
    return 0;
  }
  for (uint32_t i = 0; i < count; i++) {
    ZooExhibit exhibit;
    if (position + 12 > size) {
      return 0;
    }
    exhibit.x = (int32_t) readUint32(data, position);
    exhibit.y = (int32_t) readUint32(data, position + 4);
    uint32_t name_length = readUint32(data, position + 8);
    position += 12;
    if (exhibit.x < 0 || exhibit.x > 1000 || exhibit.y < 0 || exhibit.y > 1000 ||
        name_length > 64 || position + name_length > size) {
      return 0;
    }
    for (uint32_t c = 0; c < name_length; c++) {
      if (data[position + c] < 32 || data[position + c] >= 127) {
        return 0;
      }
    }
    exhibit.name.assign((const char *) data + position, name_length);
    position += name_length;
    // Entrance, rotation, six floats, 30 unknown bytes, extension type
    if (position + 12 + 24 + 30 + 4 > size) {
      return 0;
    }
    exhibit.entrance_x = (int32_t) readUint32(data, position);
    exhibit.entrance_y = (int32_t) readUint32(data, position + 4);
    exhibit.entrance_rotation = (int32_t) readUint32(data, position + 8);
    exhibit.current_donations = readFloat(data, position + 12);
    exhibit.last_donations = readFloat(data, position + 16);
    exhibit.total_donations = readFloat(data, position + 20);
    exhibit.current_upkeep = readFloat(data, position + 24);
    exhibit.last_upkeep = readFloat(data, position + 28);
    exhibit.total_upkeep = readFloat(data, position + 32);
    position += 36;
    // The record tail differs per format generation: the early variants
    // carry 28 more bytes, the later ones 30 bytes, an extension type and
    // extra data for tanks
    exhibit.extension_type = 0;
    if (variant == 'F' || variant == 'G') {
      position += 28;
    } else {
      position += 30;
      if (position + 4 > size) {
        return 0;
      }
      exhibit.extension_type = readUint32(data, position);
      position += 4;
      if (exhibit.extension_type == 0x10000) {
        // Tanks carry 21 more bytes
        position += 21;
      } else if (exhibit.extension_type != 0) {
        // Show tanks and unknown extensions have variable data, give up
        // on the rest of the list
        exhibits.push_back(exhibit);
        return 0;
      }
    }
    if (position > size) {
      return 0;
    }
    exhibits.push_back(exhibit);
  }
  return position;
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

  // The zoo entrance and the exhibit list follow the dimensions
  int32_t entrance_x = (int32_t) readUint32(data, dims_offset + 8);
  int32_t entrance_y = (int32_t) readUint32(data, dims_offset + 12);
  std::vector<ZooExhibit> exhibits;
  size_t exhibits_end = parseExhibits(data, size, dims_offset + 16, variant, exhibits);

  size_t tile_count = (size_t) width * (size_t) height;
  size_t terrain_size = tile_count * 10;
  size_t terrain_start = 0;
  bool found = false;
  // When the exhibit list parsed cleanly the terrain follows it, after
  // two undocumented values, and no searching is needed when the stream
  // ends on a valid object section there
  if (exhibits_end != 0) {
    for (size_t skip : {(size_t) 8, (size_t) 0}) {
      if (exhibits_end + skip + terrain_size <= size &&
          isValidObjectAnchor(data, size, exhibits_end + skip + terrain_size)) {
        terrain_start = exhibits_end + skip;
        found = true;
        break;
      }
    }
  }
  // Otherwise slide the terrain start until the end of the stream lands
  // exactly on a valid object section, exhibit records of unknown size may
  // sit between the header and the terrain.
  size_t search_end = size > terrain_size ? size - terrain_size : 0;
  if (search_end > 0x14 + 65536) {
    search_end = 0x14 + 65536;
  }
  for (size_t start = 0x14; !found && start <= search_end; start++) {
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
  zoo->entrance_x = entrance_x;
  zoo->entrance_y = entrance_y;
  zoo->exhibits = exhibits;
  zoo->tiles.reserve(tile_count);
  for (size_t i = 0; i < tile_count; i++) {
    size_t p = terrain_start + i * 10;
    ZooTerrainTile tile;
    tile.height = (int32_t) readUint32(data, p);
    tile.shape = data[p + 4];
    tile.type = data[p + 5];
    tile.edges = data[p + 6];
    memcpy(tile.padding, data + p + 7, 3);
    zoo->tiles.push_back(tile);
  }

  zoo->terrain_start = terrain_start;
  zoo->header_raw.assign(data, data + terrain_start);

  size_t object_start = terrain_start + terrain_size;
  zoo->object_count = readUint32(data, object_start);
  zoo->object_section.assign(data + object_start, data + size);
  zoo->parseObjects();
  return zoo;
}

static bool isEntityCategory(const std::string &category) {
  return category == "animals" || category == "guests" || category == "keeper" ||
         category == "maint" || category == "tour";
}

// Entity records keep their position next to their display name: the name
// is a length-prefixed printable string ("Polar Bear 1") and the position
// in 64ths of a tile, a height and the facing (0-7) sit right before it.
// The head of the record holds stats instead, so the static layout does
// not apply. Verified against every entity in the shipped maps.
static bool findEntityPosition(const uint8_t * data, size_t size, uint32_t map_size, ZooObject &object) {
  for (size_t offset = 24; offset + 4 <= size; offset++) {
    uint32_t name_length = readUint32(data, offset);
    if (name_length < 3 || name_length > 32 || offset + 4 + name_length > size || offset < 20) {
      continue;
    }
    bool printable = true;
    for (uint32_t c = 0; c < name_length && printable; c++) {
      printable = data[offset + 4 + c] >= 32 && data[offset + 4 + c] < 127;
    }
    if (!printable) {
      continue;
    }
    std::string name((const char *) data + offset + 4, name_length);
    if (name == "Male" || name == "Female") {
      continue;
    }
    uint32_t x = readUint32(data, offset - 20);
    uint32_t y = readUint32(data, offset - 16);
    if (x == 0 || y == 0 || x >= map_size * 64 || y >= map_size * 64) {
      continue;
    }
    object.x = x;
    object.y = y;
    object.elevation = (int32_t) readUint32(data, offset - 12);
    object.rotation = readUint32(data, offset - 8);
    return true;
  }
  return false;
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
    object.x = 0;
    object.y = 0;
    object.elevation = 0;
    object.rotation = 0;
    if (isEntityCategory(object.category)) {
      findEntityPosition(data + position, remaining, this->width, object);
    } else if (remaining >= 20) {
      object.x = readUint32(data, position + 4);
      object.y = readUint32(data, position + 8);
      object.elevation = (int32_t) readUint32(data, position + 12);
      object.rotation = readUint32(data, position + 16);
    }
    position += remaining;
    this->objects.push_back(object);
  }
}

std::vector<uint8_t> ZooFile::serialize() const {
  std::vector<uint8_t> data;
  data.reserve(this->header_raw.size() + this->tiles.size() * 10 + this->object_section.size());
  data.insert(data.end(), this->header_raw.begin(), this->header_raw.end());
  for (const ZooTerrainTile &tile : this->tiles) {
    uint32_t height = (uint32_t) tile.height;
    data.push_back(height & 0xFF);
    data.push_back((height >> 8) & 0xFF);
    data.push_back((height >> 16) & 0xFF);
    data.push_back((height >> 24) & 0xFF);
    data.push_back(tile.shape);
    data.push_back(tile.type);
    data.push_back(tile.edges);
    data.insert(data.end(), tile.padding, tile.padding + 3);
  }
  data.insert(data.end(), this->object_section.begin(), this->object_section.end());
  return data;
}

bool ZooFile::saveToFile(const std::string &path) const {
  FILE * fd = fopen(path.c_str(), "wb");
  if (fd == NULL) {
    SDL_Log("Could not open %s for writing", path.c_str());
    return false;
  }
  std::vector<uint8_t> data = this->serialize();
  size_t written = fwrite(data.data(), 1, data.size(), fd);
  fclose(fd);
  return written == data.size();
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
