#ifndef ZOO_FILE_HPP
#define ZOO_FILE_HPP

#include <cstdint>
#include <string>
#include <vector>

// One terrain tile, 10 bytes in the file. Heights are signed, water sits
// below zero. The stored height is the NW corner's (the corner at the
// tile's own x, y); shape holds each corner's height above a base level
// as 2 bit fields (bits 1:0 NW, 3:2 SW, 5:4 SE, 7:6 NE), with
// base = height - NW delta. The edges byte is derived data the original
// precomputes from the corner heights of each tile pair: 2 bit fields
// per edge (bits 1:0 W, 3:2 S, 5:4 E, 7:6 N), 0 flat, 1 sloped, 2 cliff
// with the neighbor higher, 3 cliff with the neighbor lower. A few stale
// edge values exist in real saves, so renderers should derive from the
// corners instead of trusting it; writers must recompute it after any
// terrain edit. The padding bytes are zero in every shipped map and save.
typedef struct {
  int32_t height;
  uint8_t shape;
  uint8_t type;
  uint8_t edges;
  uint8_t padding[3];
} ZooTerrainTile;

// One placed object. Positions are in 64ths of a tile. Only the fields
// needed for rendering are decoded, the rest of the record is kept raw.
// The elevation is the object's anchor height the original computed at
// placement time, in 16ths of a terrain height step.
typedef struct {
  std::string category;
  std::string subcategory;
  std::string code;
  uint32_t x;
  uint32_t y;
  int32_t elevation;
  uint32_t rotation;
} ZooObject;

// One exhibit, following the layout the community documented: position of
// the southernmost corner, name, entrance position and rotation and the
// running donation and upkeep numbers
typedef struct {
  int32_t x;
  int32_t y;
  std::string name;
  int32_t entrance_x;
  int32_t entrance_y;
  int32_t entrance_rotation;
  float current_donations;
  float last_donations;
  float total_donations;
  float current_upkeep;
  float last_upkeep;
  float total_upkeep;
  // 0 is a plain exhibit, tanks and show tanks carry more data
  uint32_t extension_type;
} ZooExhibit;

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

  // Serializes the zoo back into the file format. A freshly loaded file
  // writes back byte identical: the header is kept raw, the terrain is
  // re-serialized from the tiles and the object section is preserved.
  std::vector<uint8_t> serialize() const;
  bool saveToFile(const std::string &path) const;

  uint32_t getWidth() const { return this->width; }
  uint32_t getHeight() const { return this->height; }
  char getVariant() const { return this->variant; }
  uint32_t getLanguage() const { return this->language; }

  const ZooTerrainTile & getTile(uint32_t x, uint32_t y) const;

  uint32_t getObjectCount() const { return this->object_count; }
  const std::vector<uint8_t> & getObjectSection() const { return this->object_section; }
  const std::vector<ZooObject> & getObjects() const { return this->objects; }

  // The zoo entrance tile from the header
  int32_t getEntranceX() const { return this->entrance_x; }
  int32_t getEntranceY() const { return this->entrance_y; }
  const std::vector<ZooExhibit> & getExhibits() const { return this->exhibits; }

private:
  ZooFile() {}

  char variant = 0;
  uint32_t language = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  int32_t entrance_x = 0;
  int32_t entrance_y = 0;
  std::vector<ZooTerrainTile> tiles;
  std::vector<ZooExhibit> exhibits;
  uint32_t object_count = 0;
  // Everything before the terrain stream, kept raw for writing until all
  // of it is understood
  std::vector<uint8_t> header_raw;
  std::vector<uint8_t> object_section;
  std::vector<ZooObject> objects;

  void parseObjects();
  static size_t parseExhibits(const uint8_t * data, size_t size, size_t position, char variant, std::vector<ZooExhibit> &exhibits);
};

#endif // ZOO_FILE_HPP
