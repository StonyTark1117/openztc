#ifndef ZOO_FILE_HPP
#define ZOO_FILE_HPP

#include <cstdint>
#include <string>
#include <vector>

// One terrain tile, 10 bytes in the file
// Heights are signed, water sits below zero
typedef struct {
  int32_t height;
  uint8_t shape;
  uint8_t type;
  uint8_t unknown[4];
} ZooTerrainTile;

// One placed object. Positions are in 64ths of a tile. Only the fields
// needed for rendering are decoded, the rest of the record is kept raw.
typedef struct {
  std::string category;
  std::string subcategory;
  std::string code;
  uint32_t x;
  uint32_t y;
  uint32_t rotation;
} ZooObject;

// Reader for the zoo save/map format. The format starts with a TZFB magic
// followed by a variant byte which differs per game generation (F, G, S, R,
// h, i, j have been seen in shipped maps). Header sizes vary per variant,
// so the terrain stream is located by sliding its start position until the
// stream end lands exactly on a valid object section, which begins with a
// count and a length-prefixed ascii category string. All 90 maps shipped
// with the Complete Collection parse this way.
//
// The object section and everything after it is preserved raw until those
// parts of the format are implemented.
class ZooFile {
public:
  static ZooFile * loadFromMemory(const void * data, size_t size);
  static ZooFile * loadFromFile(const std::string &path);

  uint32_t getWidth() const { return this->width; }
  uint32_t getHeight() const { return this->height; }
  char getVariant() const { return this->variant; }
  uint32_t getLanguage() const { return this->language; }

  const ZooTerrainTile & getTile(uint32_t x, uint32_t y) const;

  uint32_t getObjectCount() const { return this->object_count; }
  const std::vector<uint8_t> & getObjectSection() const { return this->object_section; }
  const std::vector<ZooObject> & getObjects() const { return this->objects; }

private:
  ZooFile() {}

  char variant = 0;
  uint32_t language = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<ZooTerrainTile> tiles;
  uint32_t object_count = 0;
  std::vector<uint8_t> object_section;
  std::vector<ZooObject> objects;

  void parseObjects();
};

#endif // ZOO_FILE_HPP
