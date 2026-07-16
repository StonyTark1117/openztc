#include "MapView.hpp"

#include <algorithm>
#include <array>
#include <map>

#include "../engine/CompassDirection.hpp"
#include "../engine/Utils.hpp"

// Isometric tile size in pixels at zoom 1 and the pixel offset per height
// step. These match the original game's proportions closely enough for a
// side by side comparison.
#define TILE_HALF_WIDTH 32.0f
#define TILE_HALF_HEIGHT 16.0f
// One terrain height step in pixels at zoom 1. Measured on a controlled
// save in the original (fencetest.zoo: wall terraces at known heights):
// fence anchors drop 8px per half step and cliff faces stand ~16px tall,
// so a step is 16px — matching the stored object elevations, which are
// exactly pixels at zoom 1 (16ths of a step).
#define HEIGHT_STEP 16.0f
// One height step as a fraction of a tile width in the original's world.
// A tile draws 64 pixels wide and a step stands 16 pixels tall, which
// under the 30 degree isometric camera those two numbers imply puts the
// step at 1/sqrt(6) of a tile. Terrain lighting needs it to turn the
// height field's gradient into a world space normal.
#define HEIGHT_SCALE 0.40824829f

#define ZOOM_MIN 0.25f
#define ZOOM_MAX 3.0f

MapView::MapView(ResourceManager * resource_manager) {
  this->resource_manager = resource_manager;
}

MapView::~MapView() {
  for (auto texture_entry : this->terrain_textures) {
    if (texture_entry.second != nullptr) {
      SDL_DestroyTexture(texture_entry.second);
    }
  }
  for (IniReader * reader : this->registry_configs) {
    delete reader;
  }
  if (this->zoo != nullptr) {
    delete this->zoo;
  }
}

bool MapView::loadMap(const std::string &zoo_file_name) {
  // Map files can live inside the archives, like the scenario zoos, or as
  // loose files in the game directory, like the freeform maps
  int size = 0;
  void * content = this->resource_manager->getFileContent(zoo_file_name, &size);
  if (content != nullptr) {
    this->zoo = ZooFile::loadFromMemory(content, (size_t) size);
    free(content);
  } else {
    this->zoo = ZooFile::loadFromFile(Utils::fixPath(zoo_file_name));
  }
  if (this->zoo == nullptr) {
    SDL_Log("Could not load map %s", zoo_file_name.c_str());
    return false;
  }
  SDL_Log("Loaded map %s: %ux%u tiles, %u objects", zoo_file_name.c_str(), this->zoo->getWidth(), this->zoo->getHeight(), (uint32_t) this->zoo->getObjects().size());
  this->buildCornerHeights();
  this->buildTankWater();
  for (const ZooObject &object : this->zoo->getObjects()) {
    this->sorted_objects.push_back(&object);
  }
  this->sortObjects();
  for (const ZooObject &object : this->zoo->getObjects()) {
    if (object.category == "paths") {
      this->path_tiles[((uint64_t) (object.x / 64) << 32) | (object.y / 64)] = true;
    }
  }
  // Start looking at the middle of the map
  this->lookAtTile((float) this->zoo->getWidth() / 2.0f, (float) this->zoo->getHeight() / 2.0f);
  return true;
}

// Marine tanks are regions enclosed by tank wall pieces on the tile
// edges. Nothing in the save stores a water level: the wall art is three
// stacked glass bands of two steps each, so a tank fills to the top of
// its six step walls or back up to the rim it was dug from, whichever is
// higher — med_kids' shark tank (dug 3, glass 3 above ground) and its
// dolphin and show pools (dug 8, walls fully submerged, open water at
// the rim) both fall out of that. Flood filling the regions the wall
// edges enclose finds the interiors — the surrounding zoo reaches the
// map border, so bounded regions that carry walls are the tanks.
#define TANK_WALL_STEPS 6.0f
void MapView::buildTankWater() {
  this->tank_water_tiles.clear();
  this->tank_wall_sides.clear();
  // Wall edges by the tile north/west of them, valued at the wall's top:
  // a horizontal edge at (x, y) runs between tiles (x, y-1) and (x, y),
  // a vertical one between (x-1, y) and (x, y)
  std::unordered_map<uint64_t, float> horizontal_edges;
  std::unordered_map<uint64_t, float> vertical_edges;
  for (const ZooObject &object : this->zoo->getObjects()) {
    if (object.category != "tankwall") {
      continue;
    }
    float wall_top = (float) object.elevation / 16.0f + TANK_WALL_STEPS;
    float tile_x = SDL_roundf((float) object.x / 64.0f * 2.0f) / 2.0f;
    float tile_y = SDL_roundf((float) object.y / 64.0f * 2.0f) / 2.0f;
    std::unordered_map<uint64_t, float> &edges = tile_x != SDL_floorf(tile_x) ? horizontal_edges : vertical_edges;
    uint64_t key = ((uint64_t) (uint32_t) tile_x << 32) | (uint32_t) tile_y;
    auto found = edges.find(key);
    if (found == edges.end() || wall_top > found->second) {
      edges[key] = wall_top;
    }
  }
  if (horizontal_edges.empty() && vertical_edges.empty()) {
    return;
  }
  int width = (int) this->zoo->getWidth();
  int height = (int) this->zoo->getHeight();
  std::vector<uint8_t> visited((size_t) (width * height), 0);
  for (int start_y = 0; start_y < height; start_y++) {
    for (int start_x = 0; start_x < width; start_x++) {
      if (visited[start_y * width + start_x]) {
        continue;
      }
      // Flood fill the region, walls blocking. The border flag disqualifies
      // the open zoo without walking all of it cheaply enough.
      std::vector<uint64_t> region;
      std::vector<std::pair<int, int>> stack = {{start_x, start_y}};
      visited[start_y * width + start_x] = 1;
      bool touches_border = false;
      bool has_wall = false;
      while (!stack.empty()) {
        auto [x, y] = stack.back();
        stack.pop_back();
        region.push_back(((uint64_t) (uint32_t) x << 32) | (uint32_t) y);
        if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
          touches_border = true;
        }
        // East, south, west, north in the bitmask order
        bool wall[4] = {
          vertical_edges.contains(((uint64_t) (uint32_t) (x + 1) << 32) | (uint32_t) y),
          horizontal_edges.contains(((uint64_t) (uint32_t) x << 32) | (uint32_t) (y + 1)),
          vertical_edges.contains(((uint64_t) (uint32_t) x << 32) | (uint32_t) y),
          horizontal_edges.contains(((uint64_t) (uint32_t) x << 32) | (uint32_t) y),
        };
        static const int side_offsets[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        for (int side = 0; side < 4; side++) {
          if (wall[side]) {
            has_wall = true;
            continue;
          }
          int nx = x + side_offsets[side][0];
          int ny = y + side_offsets[side][1];
          if (nx < 0 || ny < 0 || nx >= width || ny >= height || visited[ny * width + nx]) {
            continue;
          }
          visited[ny * width + nx] = 1;
          stack.push_back({nx, ny});
        }
      }
      if (touches_border || !has_wall) {
        continue;
      }
      // A dug basin fills to the ground just outside the walls — the
      // highest tile met across a wall edge, robust against ramps dug
      // into the surroundings — and a glass tank standing on the ground
      // fills to the top of its walls, whichever is higher.
      float level = 0.0f;
      bool level_found = false;
      for (uint64_t key : region) {
        int x = (int) (key >> 32);
        int y = (int) (uint32_t) key;
        const std::unordered_map<uint64_t, float> * edge_maps[4] = {
          &vertical_edges, &horizontal_edges, &vertical_edges, &horizontal_edges,
        };
        const uint64_t edge_keys[4] = {
          ((uint64_t) (uint32_t) (x + 1) << 32) | (uint32_t) y,
          ((uint64_t) (uint32_t) x << 32) | (uint32_t) (y + 1),
          ((uint64_t) (uint32_t) x << 32) | (uint32_t) y,
          ((uint64_t) (uint32_t) x << 32) | (uint32_t) y,
        };
        static const int side_offsets[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        int sides = 0;
        for (int side = 0; side < 4; side++) {
          auto wall = edge_maps[side]->find(edge_keys[side]);
          if (wall == edge_maps[side]->end()) {
            continue;
          }
          sides |= 1 << side;
          if (!level_found || wall->second > level) {
            level = wall->second;
            level_found = true;
          }
          int ox = x + side_offsets[side][0];
          int oy = y + side_offsets[side][1];
          if (ox < 0 || oy < 0 || ox >= width || oy >= height) {
            continue;
          }
          // Tile heights are whole steps; only object elevations are 16ths
          float outside = (float) (int32_t) this->zoo->getTile((uint32_t) ox, (uint32_t) oy).height;
          if (outside > level) {
            level = outside;
          }
        }
        if (sides != 0) {
          this->tank_wall_sides[key] = sides;
        }
      }
      if (!level_found) {
        continue;
      }
      if (SDL_getenv("OPENZTC_DEBUG_TANK") != nullptr) {
        int fx = (int) (region.front() >> 32);
        int fy = (int) (uint32_t) region.front();
        SDL_Log("tank region %zu tiles near (%d, %d) level %.2f", region.size(), fx, fy, level);
      }
      for (uint64_t key : region) {
        this->tank_water_tiles[key] = level;
      }
    }
  }
  if (!this->tank_water_tiles.empty()) {
    SDL_Log("Tank water: %zu tiles across the walled basins", this->tank_water_tiles.size());
  }
}

// Applies the map orientation while projecting tile coordinates to world
// pixels. A quarter turn of the camera is a quarter turn of the tile grid
// around its origin.
void MapView::tileToWorld(float tile_x, float tile_y, float * world_x, float * world_y) {
  float u = tile_x - tile_y;
  float v = tile_x + tile_y;
  float rotated_u;
  float rotated_v;
  switch (this->orientation & 3) {
    case 1:
      rotated_u = v;
      rotated_v = -u;
      break;
    case 2:
      rotated_u = -u;
      rotated_v = -v;
      break;
    case 3:
      rotated_u = -v;
      rotated_v = u;
      break;
    default:
      rotated_u = u;
      rotated_v = v;
      break;
  }
  *world_x = rotated_u * TILE_HALF_WIDTH;
  *world_y = rotated_v * TILE_HALF_HEIGHT;
}

void MapView::tileToUnit(float tile_x, float tile_y, float * unit_x, float * unit_y) {
  float world_x;
  float world_y;
  this->tileToWorld(tile_x, tile_y, &world_x, &world_y);
  // Bounds of the rotated map in world pixels
  float width = (float) this->getMapWidth();
  float height = (float) this->getMapHeight();
  float min_x = 0.0f;
  float max_x = 0.0f;
  float min_y = 0.0f;
  float max_y = 0.0f;
  const float corners[4][2] = {{0.0f, 0.0f}, {width, 0.0f}, {width, height}, {0.0f, height}};
  for (int i = 0; i < 4; i++) {
    float corner_x;
    float corner_y;
    this->tileToWorld(corners[i][0], corners[i][1], &corner_x, &corner_y);
    min_x = SDL_min(min_x, corner_x);
    max_x = SDL_max(max_x, corner_x);
    min_y = SDL_min(min_y, corner_y);
    max_y = SDL_max(max_y, corner_y);
  }
  *unit_x = max_x > min_x ? (world_x - min_x) / (max_x - min_x) : 0.0f;
  *unit_y = max_y > min_y ? (world_y - min_y) / (max_y - min_y) : 0.0f;
}

void MapView::screenToTile(float screen_x, float screen_y, SDL_FRect * window_rect, float * tile_x, float * tile_y) {
  float world_x = (screen_x - window_rect->w / 2.0f) / this->zoom + this->camera_x;
  float world_y = (screen_y - window_rect->h / 2.0f) / this->zoom + this->camera_y;
  float rotated_u = world_x / TILE_HALF_WIDTH;
  float rotated_v = world_y / TILE_HALF_HEIGHT;
  float u;
  float v;
  switch (this->orientation & 3) {
    case 1:
      u = -rotated_v;
      v = rotated_u;
      break;
    case 2:
      u = -rotated_u;
      v = -rotated_v;
      break;
    case 3:
      u = rotated_v;
      v = -rotated_u;
      break;
    default:
      u = rotated_u;
      v = rotated_v;
      break;
  }
  *tile_x = (u + v) / 2.0f;
  *tile_y = (v - u) / 2.0f;
}

// Reads a color list like the miniclr sections have
static bool sectionColor(IniReader * reader, const std::string &section, uint8_t * rgba) {
  std::vector<int> values = reader->getIntList(section, "color");
  if (values.size() != 3) {
    return false;
  }
  rgba[0] = (uint8_t) values[0];
  rgba[1] = (uint8_t) values[1];
  rgba[2] = (uint8_t) values[2];
  rgba[3] = 255;
  return true;
}

std::vector<uint8_t> MapView::buildMinimapColors(IniReader * minimap_colors) {
  std::vector<uint8_t> colors;
  if (this->zoo == nullptr || minimap_colors == nullptr) {
    return colors;
  }
  uint32_t width = this->zoo->getWidth();
  uint32_t height = this->zoo->getHeight();
  colors.assign((size_t) width * height * 4, 255);

  // Terrain type numbers to miniclr sections: the tiletex sections carry
  // the type and their names minus the tt prefix name the color sections
  std::unordered_map<int, std::array<uint8_t, 4>> terrain_colors;
  for (std::string config_name : this->resource_manager->getResourceNamesWithExtension("CFG")) {
    if (config_name.rfind("terrain/tiletex", 0) != 0) {
      continue;
    }
    IniReader * reader = this->resource_manager->getIniReader(config_name);
    if (reader == nullptr) {
      continue;
    }
    for (std::string section : reader->getSections()) {
      int type = reader->getInt(section, "type", -1);
      if (type < 0 || terrain_colors.contains(type)) {
        continue;
      }
      std::string color_section = section.rfind("tt", 0) == 0 ? section.substr(2) : section;
      std::array<uint8_t, 4> color;
      if (sectionColor(minimap_colors, color_section, color.data())) {
        terrain_colors[type] = color;
      }
    }
    delete reader;
  }
  for (uint32_t tile_y = 0; tile_y < height; tile_y++) {
    for (uint32_t tile_x = 0; tile_x < width; tile_x++) {
      auto entry = terrain_colors.find(this->zoo->getTile(tile_x, tile_y).type);
      if (entry != terrain_colors.end()) {
        SDL_memcpy(&colors[((size_t) tile_y * width + tile_x) * 4], entry->second.data(), 4);
      }
    }
  }

  // Objects paint over the terrain in their color class
  for (const ZooObject &object : this->zoo->getObjects()) {
    std::string color_section;
    if (object.category == "paths") {
      color_section = "path";
    } else if (object.category == "fences" || object.category == "tankwall") {
      color_section = object.subcategory == "zoowall" ? "zoowall" : "habitat";
    } else if (object.category == "building") {
      color_section = "buildings";
    } else if (object.category == "objects" && object.subcategory == "foliage") {
      color_section = "foliage";
    } else {
      continue;
    }
    uint8_t color[4];
    if (!sectionColor(minimap_colors, color_section, color)) {
      continue;
    }
    uint32_t tile_x = object.x / 64;
    uint32_t tile_y = object.y / 64;
    if (tile_x < width && tile_y < height) {
      SDL_memcpy(&colors[((size_t) tile_y * width + tile_x) * 4], color, 4);
    }
  }
  return colors;
}

std::vector<uint64_t> MapView::getPathTileKeys() {
  std::vector<uint64_t> keys;
  keys.reserve(this->path_tiles.size());
  for (auto entry : this->path_tiles) {
    keys.push_back(entry.first);
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

// Paint order: back to front by screen depth rows of whole tiles. A fence
// piece separates its two flanking tiles, so it draws after everything in
// the tile behind it and before everything in the tile in front — a
// continuous depth sort by anchor cannot express that and lets scenery in
// the front tile slip behind the fence.
void MapView::sortObjects() {
  struct SortKey {
    int row;
    int column;
    int phase;
    float depth;
  };
  std::unordered_map<const ZooObject *, SortKey> keys;
  keys.reserve(this->sorted_objects.size());
  for (const ZooObject * object : this->sorted_objects) {
    float tile_x = (float) object->x / 64.0f;
    float tile_y = (float) object->y / 64.0f;
    float world_x;
    float depth;
    this->tileToWorld(tile_x, tile_y, &world_x, &depth);
    SortKey key;
    key.depth = depth;
    if (object->category == "fences" || object->category == "tankwall") {
      // The two flanking tile centers; the piece draws right after the
      // one further back
      float fraction_x = (float) (object->x % 64) / 64.0f;
      bool y_run = fraction_x <= 0.25f || fraction_x >= 0.75f;
      float ax;
      float ay;
      float bx;
      float by;
      if (y_run) {
        float edge_x = (float) (int) (tile_x + 0.5f);
        ax = edge_x - 0.5f;
        ay = (float) (int) tile_y + 0.5f;
        bx = edge_x + 0.5f;
        by = ay;
      } else {
        float edge_y = (float) (int) (tile_y + 0.5f);
        ax = (float) (int) tile_x + 0.5f;
        ay = edge_y - 0.5f;
        bx = ax;
        by = edge_y + 0.5f;
      }
      float depth_a;
      float depth_b;
      this->tileToWorld(ax, ay, &world_x, &depth_a);
      this->tileToWorld(bx, by, &world_x, &depth_b);
      // The piece belongs to the further-back flanking tile and draws
      // right after that tile's own objects: the original hides a rock in
      // the tile behind a wall, yet a tree in the diagonal neighbor of the
      // same row still droops its crown over the wall (it draws with its
      // own, later tile)
      float back_x;
      float back_depth;
      if (depth_a <= depth_b) {
        this->tileToWorld(ax, ay, &back_x, &back_depth);
      } else {
        this->tileToWorld(bx, by, &back_x, &back_depth);
      }
      key.row = (int) SDL_lroundf(back_depth / TILE_HALF_HEIGHT);
      key.column = (int) SDL_lroundf(back_x / TILE_HALF_WIDTH);
      key.phase = 1;
    } else {
      // The object's whole tile decides its row
      float center_x = (float) (int) tile_x + 0.5f;
      float center_y = (float) (int) tile_y + 0.5f;
      float center_world_x;
      float center_depth;
      this->tileToWorld(center_x, center_y, &center_world_x, &center_depth);
      key.row = (int) SDL_lroundf(center_depth / TILE_HALF_HEIGHT);
      key.column = (int) SDL_lroundf(center_world_x / TILE_HALF_WIDTH);
      key.phase = 0;
    }
    keys[object] = key;
  }
  std::sort(this->sorted_objects.begin(), this->sorted_objects.end(),
            [&keys](const ZooObject * a, const ZooObject * b) {
              const SortKey &ka = keys[a];
              const SortKey &kb = keys[b];
              if (ka.row != kb.row) {
                return ka.row < kb.row;
              }
              if (ka.column != kb.column) {
                return ka.column < kb.column;
              }
              if (ka.phase != kb.phase) {
                return ka.phase < kb.phase;
              }
              return ka.depth < kb.depth;
            });
  // The paint keys aligned with the sorted order, for interleaving the
  // tank water surface, and the water tiles in the same order. The
  // surface draws at phase 2: after a tile's own objects and after the
  // wall on its near edge, which a dug tank keeps below the surface.
  this->sorted_object_keys.clear();
  this->sorted_object_keys.reserve(this->sorted_objects.size());
  for (const ZooObject * object : this->sorted_objects) {
    const SortKey &key = keys[object];
    this->sorted_object_keys.push_back({key.row, key.column, key.phase});
  }
  this->water_draws.clear();
  for (const auto &entry : this->tank_water_tiles) {
    WaterDraw draw;
    draw.tile_x = (float) (int) (entry.first >> 32);
    draw.tile_y = (float) (int) (uint32_t) entry.first;
    draw.level = entry.second;
    float center_world_x;
    float center_depth;
    this->tileToWorld(draw.tile_x + 0.5f, draw.tile_y + 0.5f, &center_world_x, &center_depth);
    draw.key.row = (int) SDL_lroundf(center_depth / TILE_HALF_HEIGHT);
    draw.key.column = (int) SDL_lroundf(center_world_x / TILE_HALF_WIDTH);
    draw.key.phase = 2;
    this->water_draws.push_back(draw);
  }
  std::sort(this->water_draws.begin(), this->water_draws.end(),
            [](const WaterDraw &a, const WaterDraw &b) {
              if (a.key.row != b.key.row) {
                return a.key.row < b.key.row;
              }
              if (a.key.column != b.key.column) {
                return a.key.column < b.key.column;
              }
              return a.key.phase < b.key.phase;
            });
  if (SDL_getenv("OPENZTC_DEBUG_SORT") != nullptr) {
    int index = 0;
    for (const ZooObject * object : this->sorted_objects) {
      const SortKey &key = keys[object];
      float tx = (float) object->x / 64.0f;
      float ty = (float) object->y / 64.0f;
      if (tx >= 12.0f && tx <= 19.0f && ty >= 43.0f && ty <= 47.0f) {
        SDL_Log("sort %4d row %3d phase %d depth %7.1f %s/%s (%.2f, %.2f)", index, key.row,
                key.phase, key.depth, object->category.c_str(), object->code.c_str(), tx, ty);
      }
      index++;
    }
  }
}

void MapView::setOrientation(int orientation) {
  this->orientation = ((orientation % 4) + 4) % 4;
  if (this->zoo != nullptr) {
    this->sortObjects();
    this->lookAtTile((float) this->zoo->getWidth() / 2.0f, (float) this->zoo->getHeight() / 2.0f);
  }
}

// Exact corner heights from the terrain shape byte: four 2 bit fields hold
// each corner's height above a base level (bits 1:0 NW at the tile's x,y,
// 3:2 SW, 5:4 SE, 7:6 NE), and the stored tile height is the NW corner's
// height, so base = height - NW delta. Decoded by raising known plateaus
// in the original and diffing the saves. Adjacent tiles disagree about a
// shared corner exactly where the original shows a cliff face.
void MapView::buildCornerHeights() {
  uint32_t width = this->zoo->getWidth();
  uint32_t height = this->zoo->getHeight();
  this->corner_heights.assign((size_t) (width + 1) * (height + 1), 0.0f);
  this->tile_corner_heights.assign((size_t) width * height * 4, 0.0f);
  for (uint32_t tile_y = 0; tile_y < height; tile_y++) {
    for (uint32_t tile_x = 0; tile_x < width; tile_x++) {
      const ZooTerrainTile &tile = this->zoo->getTile(tile_x, tile_y);
      float base = (float) tile.height - (float) (tile.shape & 3);
      float nw = (float) tile.height;
      float sw = base + (float) ((tile.shape >> 2) & 3);
      float se = base + (float) ((tile.shape >> 4) & 3);
      float ne = base + (float) ((tile.shape >> 6) & 3);
      size_t index = ((size_t) tile_y * width + tile_x) * 4;
      this->tile_corner_heights[index] = nw;
      this->tile_corner_heights[index + 1] = ne;
      this->tile_corner_heights[index + 2] = se;
      this->tile_corner_heights[index + 3] = sw;
      // The shared grid keeps one claim per corner for anchoring and
      // shading; each tile writes all four so the last row and column of
      // corners get values too
      this->corner_heights[(size_t) tile_y * (width + 1) + tile_x] = nw;
      this->corner_heights[(size_t) tile_y * (width + 1) + tile_x + 1] = ne;
      this->corner_heights[(size_t) (tile_y + 1) * (width + 1) + tile_x] = sw;
      this->corner_heights[(size_t) (tile_y + 1) * (width + 1) + tile_x + 1] = se;
    }
  }
}

// Corner heights of one tile in the same order the tile quads use:
// 0 NW (x, y), 1 NE, 2 SE, 3 SW
float MapView::tileCornerHeight(uint32_t x, uint32_t y, int corner) {
  return this->tile_corner_heights[((size_t) y * this->zoo->getWidth() + x) * 4 + corner];
}

// The terrain height at a fractional tile position, interpolated across
// the tile's corners, for anchoring sprites on slopes
float MapView::heightAt(float tile_x, float tile_y) {
  uint32_t x = (uint32_t) tile_x;
  uint32_t y = (uint32_t) tile_y;
  if (x >= this->zoo->getWidth() || y >= this->zoo->getHeight()) {
    return 0.0f;
  }
  float fx = tile_x - (float) x;
  float fy = tile_y - (float) y;
  size_t index = ((size_t) y * this->zoo->getWidth() + x) * 4;
  float nw = this->tile_corner_heights[index];
  float ne = this->tile_corner_heights[index + 1];
  float se = this->tile_corner_heights[index + 2];
  float sw = this->tile_corner_heights[index + 3];
  float north = nw + (ne - nw) * fx;
  float south = sw + (se - sw) * fx;
  return north + (south - north) * fy;
}

// Whether the tile under an entity position (in 64ths of a tile) is a
// water type, which switches animals to their swimming art
bool MapView::isWaterAt(int32_t x, int32_t y) {
  if (x < 0 || y < 0) {
    return false;
  }
  uint32_t tile_x = (uint32_t) x / 64;
  uint32_t tile_y = (uint32_t) y / 64;
  if (tile_x >= this->zoo->getWidth() || tile_y >= this->zoo->getHeight()) {
    return false;
  }
  return this->water_terrain_types.contains(this->zoo->getTile(tile_x, tile_y).type);
}

// The first candidate animation that exists, cached under the given key —
// including the misses, so absent art is only searched once
Animation * MapView::firstAnimation(const std::vector<std::string> &candidates, const std::string &cache_key) {
  if (this->object_animations.contains(cache_key)) {
    return this->object_animations[cache_key];
  }
  Animation * found = nullptr;
  for (const std::string &candidate : candidates) {
    found = this->resource_manager->getAnimation(candidate);
    if (found != nullptr) {
      break;
    }
  }
  this->object_animations[cache_key] = found;
  if (found == nullptr) {
    this->missing_object_art++;
  }
  return found;
}

// The terrain heights at the two grid corners a fence piece's edge spans.
// Pieces at a half tile x sit on a y axis edge and the other way around;
// start is the corner with the smaller coordinate along the run. Each of
// the two flanking tiles has its own idea of the edge's two corner
// heights (they disagree across a cliff). The original slopes the fence
// exactly where a flanking tile sees the edge itself sloping one step:
// on deathmtn's cliff base the west tile slopes 2->1 and the wall is a
// 30 degree piece, while a hard terrace cliff (both flanks flat, traced
// on the fencetest save) keeps the wall flat. Start/end report the
// sloping flank's pair when one exists.
void MapView::fenceEdgeHeights(const ZooObject * object, float * start, float * end, bool * y_run_out,
                               float * ground) {
  *start = 0.0f;
  *end = 0.0f;
  int width = (int) this->zoo->getWidth();
  int height = (int) this->zoo->getHeight();
  float fraction_x = (float) (object->x % 64) / 64.0f;
  bool y_run = fraction_x <= 0.25f || fraction_x >= 0.75f;
  if (y_run_out != nullptr) {
    *y_run_out = y_run;
  }
  // Flank tiles and the corner indices (0 NW, 1 NE, 2 SE, 3 SW) that make
  // up the edge in each, ordered start then end along the run
  int flank_x[2];
  int flank_y[2];
  int corner_a[2];
  int corner_b[2];
  if (y_run) {
    int edge_x = (int) (((float) object->x / 64.0f) + 0.5f);
    int tile_y = (int) (object->y / 64);
    flank_x[0] = edge_x - 1; flank_y[0] = tile_y; corner_a[0] = 1; corner_b[0] = 2;  // west tile: NE, SE
    flank_x[1] = edge_x;     flank_y[1] = tile_y; corner_a[1] = 0; corner_b[1] = 3;  // east tile: NW, SW
  } else {
    int tile_x = (int) (object->x / 64);
    int edge_y = (int) (((float) object->y / 64.0f) + 0.5f);
    flank_x[0] = tile_x; flank_y[0] = edge_y - 1; corner_a[0] = 3; corner_b[0] = 2;  // north tile: SW, SE
    flank_x[1] = tile_x; flank_y[1] = edge_y;     corner_a[1] = 0; corner_b[1] = 1;  // south tile: NW, NE
  }
  bool have = false;
  for (int f = 0; f < 2; f++) {
    if (flank_x[f] < 0 || flank_y[f] < 0 || flank_x[f] >= width || flank_y[f] >= height) {
      continue;
    }
    float a = this->tileCornerHeight((uint32_t) flank_x[f], (uint32_t) flank_y[f], corner_a[f]);
    float b = this->tileCornerHeight((uint32_t) flank_x[f], (uint32_t) flank_y[f], corner_b[f]);
    if (!have) {
      *start = a;
      *end = b;
      have = true;
    }
    if (a != b) {
      // A sloping flank decides the piece's slope and direction
      *start = a;
      *end = b;
      break;
    }
  }
  if (ground != nullptr) {
    *ground = SDL_min(*start, *end);
  }
}

float MapView::cornerHeight(uint32_t x, uint32_t y) {
  return this->corner_heights[(size_t) y * (this->zoo->getWidth() + 1) + x];
}

void MapView::lookAtTile(float tile_x, float tile_y) {
  this->tileToWorld(tile_x, tile_y, &this->camera_x, &this->camera_y);
}

bool MapView::handleInputs(std::vector<Input> &inputs) {
  for (Input input : inputs) {
    switch (input.event) {
      case InputEvent::BACK:
        return false;
      case InputEvent::LEFT_CLICK:
        this->dragging = true;
        this->last_cursor = input.position;
        break;
      case InputEvent::LEFT_RELEASE:
        this->dragging = false;
        break;
      case InputEvent::CURSOR_MOVE:
        if (this->dragging) {
          this->camera_x -= (input.position.x - this->last_cursor.x) / this->zoom;
          this->camera_y -= (input.position.y - this->last_cursor.y) / this->zoom;
          this->last_cursor = input.position;
        }
        break;
      case InputEvent::ZOOM_IN:
      case InputEvent::ZOOM_OUT:
      case InputEvent::SCROLL: {
        bool in = input.event == InputEvent::ZOOM_IN || (input.event == InputEvent::SCROLL && input.scroll > 0.0f);
        this->zoom *= in ? 1.15f : 1.0f / 1.15f;
        if (this->zoom < ZOOM_MIN) {
          this->zoom = ZOOM_MIN;
        }
        if (this->zoom > ZOOM_MAX) {
          this->zoom = ZOOM_MAX;
        }
        break;
      }
      default:
        break;
    }
  }
  return true;
}

// The original's terrain light rig lives in terrain/tilevar.cfg: two
// directional lights and a material, which the game hands to Direct3D to
// light the terrain mesh per vertex. Reading it here keeps the rig the
// data's business, so a mod that retunes the lights retunes ours too.
void MapView::loadTerrainLights() {
  IniReader * reader = this->resource_manager->getIniReader("terrain/tilevar.cfg");
  if (reader == nullptr) {
    return;
  }
  auto readFloat = [&](const std::string &section, const std::string &key, float fallback) -> float {
    std::string value = reader->get(section, key);
    return value.empty() ? fallback : (float) atof(value.c_str());
  };
  // The rig is grey: the configs light every channel alike, so one
  // number per color carries it
  this->material_diffuse = readFloat("mtrl.diffuse", "R", 1.0f);
  this->material_ambient = readFloat("mtrl.ambient", "R", 1.0f);
  this->terrain_lights.clear();
  std::vector<std::string> sections = reader->getSections();
  for (int index = 0;; index++) {
    std::string name = "light" + std::to_string(index);
    if (std::find(sections.begin(), sections.end(), name + ".direction") == sections.end()) {
      break;
    }
    TerrainLight light;
    light.direction[0] = readFloat(name + ".direction", "X", 0.0f);
    light.direction[1] = readFloat(name + ".direction", "Y", -1.0f);
    light.direction[2] = readFloat(name + ".direction", "Z", 0.0f);
    float length = SDL_sqrtf(light.direction[0] * light.direction[0] +
                             light.direction[1] * light.direction[1] +
                             light.direction[2] * light.direction[2]);
    if (length <= 0.0f) {
      continue;
    }
    for (int axis = 0; axis < 3; axis++) {
      light.direction[axis] /= length;
    }
    light.diffuse = readFloat(name + ".diffuse", "R", 0.0f);
    light.ambient = readFloat(name + ".ambient", "R", 0.0f);
    this->terrain_lights.push_back(light);
  }
  delete reader;
  SDL_Log("Loaded %zu terrain lights, flat ground lights to %.4f", this->terrain_lights.size(),
          this->terrainBrightness(0.0f, 0.0f));
}

float MapView::terrainBrightness(float gradient_x, float gradient_y) {
  if (this->terrain_lights.empty()) {
    return 1.0f;
  }
  // The original's world has y up, the way its light directions read, so
  // the height field's normal is (-dy/dx, 1, -dy/dz) with the gradients
  // scaled from height steps per tile into world units.
  // Both lights lie in the world's xy plane, so only the slope along the
  // world's x axis changes the light and a slope along z merely tilts the
  // normal away from both. The original's x axis runs along the map's
  // negative y and its z along the map's x: its cliff face art lights the
  // faces pointing down the screen along +y and shades those pointing
  // along -x, and Death Mountain shows the same faces at 89 against 62
  // luminance. Only this mapping puts the light on that side.
  float world_gradient_x = -gradient_y;
  float world_gradient_z = gradient_x;
  float normal_x = -world_gradient_x * HEIGHT_SCALE;
  float normal_y = 1.0f;
  float normal_z = -world_gradient_z * HEIGHT_SCALE;
  float length = SDL_sqrtf(normal_x * normal_x + normal_y * normal_y + normal_z * normal_z);
  normal_x /= length;
  normal_y /= length;
  normal_z /= length;

  float light = 0.0f;
  for (const TerrainLight &terrain_light : this->terrain_lights) {
    light += this->material_ambient * terrain_light.ambient;
    // A Direct3D light direction points the way the light travels, so
    // the surface faces the light when the dot product turns negative
    float dot = -(normal_x * terrain_light.direction[0] + normal_y * terrain_light.direction[1] +
                  normal_z * terrain_light.direction[2]);
    if (dot > 0.0f) {
      light += this->material_diffuse * terrain_light.diffuse * dot;
    }
  }
  return std::clamp(light, 0.0f, 1.0f);
}

void MapView::loadTerrainTextures(SDL_Renderer * renderer) {
  this->textures_loaded = true;
  this->loadTerrainLights();
  // Terrain types and their textures are defined in the tiletex config
  // files, expansions add their own
  for (std::string config_name : this->resource_manager->getResourceNamesWithExtension("CFG")) {
    if (config_name.rfind("terrain/tiletex", 0) != 0) {
      continue;
    }
    IniReader * reader = this->resource_manager->getIniReader(config_name);
    if (reader == nullptr) {
      continue;
    }
    for (std::string section : reader->getSections()) {
      int type = reader->getInt(section, "type", -1);
      std::string texture_name = reader->get(section, "texture");
      if (type >= 0 && reader->getInt(section, "water", 0) > 0) {
        this->water_terrain_types.insert(type);
      }
      if (type >= 0 && reader->getInt(section, "blend", 1) == 0) {
        this->unblended_terrain_types.insert(type);
      }
      if (type < 0 || texture_name.empty() || this->terrain_textures.contains(type)) {
        continue;
      }
      SDL_Texture * texture = this->resource_manager->getTexture(renderer, texture_name);
      if (texture != nullptr) {
        // Boundary blending draws neighbor textures with vertex alpha
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        // Point sampling: the diamond UV mapping rarely lands on texel
        // centers, and the linear filter was washing out the ground
        // grain (measured: half the original's local contrast on sand)
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        // The original holds its terrain art at 128 texels to the tile:
        // every ground type ships at 128x128 and covers one tile, while
        // water and concrete ship at 256x256 and stretch over two tiles
        // each way. Squeezing those onto a single tile made their grain
        // twice as fine as the original's.
        float texture_width = 0.0f;
        float texture_height = 0.0f;
        SDL_GetTextureSize(texture, &texture_width, &texture_height);
        int span = (int) SDL_lroundf(texture_width / 128.0f);
        this->terrain_texture_span[type] = SDL_max(span, 1);
        this->terrain_textures[type] = texture;
      }
    }
    delete reader;
  }
  // The rocky wedges the original draws over cliff edges, one sprite per
  // height step of a tile edge. "l" faces descend to the right, "r" to
  // the left, and the digits are the corner pattern of the drop.
  static const char * fringe_names[] = {"l0001", "l0010", "l0011", "l0110", "l1011",
                                        "r0001", "r0010", "r0011", "r0110", "r1011"};
  for (const char * name : fringe_names) {
    SDL_Texture * texture =
        this->resource_manager->getTexture(renderer, std::string("fringe/") + name + ".bmp");
    if (texture != nullptr) {
      SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
      // The sprites carry their own light and shade, the bright side and
      // the dark side; the terrain's flat light still scales them (their
      // art reads 4 percent brighter than the original draws them)
      SDL_SetTextureColorMod(texture, 245, 245, 245);
      this->fringe_textures[name] = texture;
    }
  }
  SDL_Log("Loaded %i terrain textures, %i fringe sprites", (int) this->terrain_textures.size(),
          (int) this->fringe_textures.size());
}

void MapView::draw(SDL_Renderer * renderer, SDL_FRect * window_rect) {
  if (this->zoo == nullptr) {
    return;
  }
  if (!this->textures_loaded) {
    this->loadTerrainTextures(renderer);
  }

  float center_x = window_rect->w / 2.0f;
  float center_y = window_rect->h / 2.0f;
  uint32_t width = this->zoo->getWidth();
  uint32_t height = this->zoo->getHeight();

  // Batch the tile quads per terrain texture to keep the draw call count
  // independent of the map size. Boundary blending triangles go into their
  // own batches, drawn after all the base tiles.
  std::unordered_map<int, std::vector<SDL_Vertex>> batches;
  std::unordered_map<int, std::vector<int>> batch_indices;
  std::map<int, std::vector<SDL_Vertex>> blend_batches;
  std::map<int, std::vector<int>> blend_indices;
  std::unordered_map<int, std::vector<SDL_Vertex>> cliff_batches;
  std::unordered_map<int, std::vector<int>> cliff_indices;
  // Cliff faces are the original's fringe sprites, collected here and
  // drawn before the tile tops like the old face quads were
  struct FringeDraw {
    std::string name;
    SDL_FRect rect;
  };
  std::vector<FringeDraw> fringe_draws;

  // The tile type at a position, or -1 off the map
  auto typeAt = [&](int x, int y) -> int {
    if (x < 0 || y < 0 || x >= (int) width || y >= (int) height) {
      return -1;
    }
    return (int) this->zoo->getTile(x, y).type;
  };
  // Whether any of the four tiles meeting at grid corner (cx, cy) has the
  // wanted type
  // How many of the four tiles meeting at a grid corner carry a type. The
  // original weighs a corner by exactly this, so a corner where one tile in
  // four is gray rock is a quarter gray rock.
  auto cornerTypeCount = [&](int cx, int cy, int wanted) -> int {
    return (typeAt(cx - 1, cy - 1) == wanted ? 1 : 0) + (typeAt(cx, cy - 1) == wanted ? 1 : 0) +
           (typeAt(cx - 1, cy) == wanted ? 1 : 0) + (typeAt(cx, cy) == wanted ? 1 : 0);
  };

  for (uint32_t tile_y = 0; tile_y < height; tile_y++) {
    for (uint32_t tile_x = 0; tile_x < width; tile_x++) {
      const ZooTerrainTile &tile = this->zoo->getTile(tile_x, tile_y);

      SDL_FPoint corners[4];
      float corner_h[4];
      const int corner_offsets[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
      bool visible = false;
      for (int i = 0; i < 4; i++) {
        uint32_t cx = tile_x + corner_offsets[i][0];
        uint32_t cy = tile_y + corner_offsets[i][1];
        corner_h[i] = this->tileCornerHeight(tile_x, tile_y, i);
        float world_x;
        float world_y;
        this->tileToWorld((float) cx, (float) cy, &world_x, &world_y);
        world_y -= corner_h[i] * HEIGHT_STEP;
        corners[i].x = (world_x - this->camera_x) * this->zoom + center_x;
        corners[i].y = (world_y - this->camera_y) * this->zoom + center_y;
        if (corners[i].x > -100.0f && corners[i].x < window_rect->w + 100.0f &&
            corners[i].y > -100.0f && corners[i].y < window_rect->h + 100.0f) {
          visible = true;
        }
      }
      if (!visible) {
        continue;
      }

      // A vertical cliff face wherever the two tiles sharing an edge
      // disagree about its corner heights. The east and south edge of
      // every tile covers all edges once; the face takes the higher
      // tile's texture. Cliff batches draw before every tile top, which
      // covers the faces pointing away from the camera.
      for (int edge = 0; edge < 2; edge++) {
        // east edge: my NE(1)+SE(2) against neighbor (x+1, y) NW(0)+SW(3)
        // south edge: my SW(3)+SE(2) against neighbor (x, y+1) NW(0)+NE(1)
        int nx = (int) tile_x + (edge == 0 ? 1 : 0);
        int ny = (int) tile_y + (edge == 0 ? 0 : 1);
        if (nx >= (int) width || ny >= (int) height) {
          continue;
        }
        int mine_a = edge == 0 ? 1 : 3;
        int mine_b = 2;
        int theirs_a = 0;
        int theirs_b = edge == 0 ? 3 : 1;
        float mine_h_a = corner_h[mine_a];
        float mine_h_b = corner_h[mine_b];
        float theirs_h_a = this->tileCornerHeight((uint32_t) nx, (uint32_t) ny, theirs_a);
        float theirs_h_b = this->tileCornerHeight((uint32_t) nx, (uint32_t) ny, theirs_b);
        if (mine_h_a == theirs_h_a && mine_h_b == theirs_h_b) {
          continue;
        }
        bool mine_higher = mine_h_a + mine_h_b > theirs_h_a + theirs_h_b;
        uint8_t face_type_raw = mine_higher ? tile.type : this->zoo->getTile(nx, ny).type;
        int cliff_type = this->terrain_textures.contains(face_type_raw) ? face_type_raw : 0;
        float high_a = mine_higher ? mine_h_a : theirs_h_a;
        float high_b = mine_higher ? mine_h_b : theirs_h_b;
        float low_a = mine_higher ? theirs_h_a : mine_h_a;
        float low_b = mine_higher ? theirs_h_b : mine_h_b;
        // The shared edge's screen position, at the high heights
        float ax = corners[mine_a].x;
        float bx = corners[mine_b].x;
        float ay_high = corners[mine_a].y - (high_a - corner_h[mine_a]) * HEIGHT_STEP * this->zoom;
        float by_high = corners[mine_b].y - (high_b - corner_h[mine_b]) * HEIGHT_STEP * this->zoom;
        float ay_low = ay_high + (high_a - low_a) * HEIGHT_STEP * this->zoom;
        float by_low = by_high + (high_b - low_b) * HEIGHT_STEP * this->zoom;
        // The original does not texture cliff faces with the terrain at
        // all: it draws the rocky fringe sprites from fringe.ztd, the
        // same brown rock whatever the terrain type (verified by loading
        // one plateau save painted sand and again painted snow — both
        // render identical faces). One sprite spans a single height step
        // over one tile edge, so a taller drop stacks copies.
        (void) cliff_type;
        // The sprite whose top edge falls the way this edge does: the
        // "r" art descends to the left, "l" to the right
        bool a_left = ax < bx;
        float left_x = a_left ? ax : bx;
        float left_y_high = a_left ? ay_high : by_high;
        float left_drop = (a_left ? ay_low - ay_high : by_low - by_high);
        // Which way the edge itself runs decides the art, and that is a
        // question about the tile grid rather than about the terrain: ask
        // the corners where they sit with no height on them. Reading it
        // off the high edge instead let a flank that slopes a step tilt
        // the edge from a half step per pixel to flat or to a whole one,
        // which flips the comparison and picks the mirrored art.
        float base_a_x;
        float base_a_y;
        float base_b_x;
        float base_b_y;
        this->tileToWorld((float) ((int) tile_x + corner_offsets[mine_a][0]),
                          (float) ((int) tile_y + corner_offsets[mine_a][1]), &base_a_x, &base_a_y);
        this->tileToWorld((float) ((int) tile_x + corner_offsets[mine_b][0]),
                          (float) ((int) tile_y + corner_offsets[mine_b][1]), &base_b_x, &base_b_y);
        float left_base_y = a_left ? base_a_y : base_b_y;
        float right_base_y = a_left ? base_b_y : base_a_y;
        // The "l" art descends to the right, the "r" art climbs
        const char * side = right_base_y > left_base_y ? "l" : "r";
        float step_px = HEIGHT_STEP * this->zoom;
        int left_steps = (int) SDL_lroundf(left_drop / step_px);
        // Only the level part of a drop is a band. Where a flank slopes, the
        // step it slopes through is a wedge, and fringe.ztd carries one for
        // each way a single flank can lean. Stacking bands to the deeper
        // corner instead left the wedge's triangle unpainted, which showed
        // through as black.
        //
        // Each piece carries its art at its own height inside the 64 pixel
        // canvas, so they cannot share one top: a piece is placed by lining
        // its own high edge up with where the face has got to, and it then
        // advances that by however many steps it spans at the left.
        struct FringePiece {
          const char * digits;
          int left_steps;
          float art_top_r;
          float art_top_l;
        };
        // The l art sits a pixel lower in its canvas than a clean mirror of
        // the r art would, so its offsets are taken from where the band has
        // always been placed rather than from the measured pixel.
        static const FringePiece pieces[] = {
          {"0011", 1, 16.0f, 0.0f},   // both flanks level: the band
          {"0001", 0, 31.0f, 15.0f},  // high flank rises to the right
          {"0010", 1, 16.0f, 0.0f},   // high flank falls to the right
          {"0110", 1, 16.0f, 0.0f},   // low flank rises to the right
          {"1011", 0, 31.0f, 15.0f},  // low flank falls to the right
        };
        float high_left = a_left ? high_a : high_b;
        float high_right = a_left ? high_b : high_a;
        float low_left = a_left ? low_a : low_b;
        float low_right = a_left ? low_b : low_a;
        int high_tilt = (int) SDL_lroundf(high_right - high_left);
        int low_tilt = (int) SDL_lroundf(low_right - low_left);
        bool right_side = side[0] == 'r';
        float cursor = left_y_high;
        auto place = [&](const char * digits) {
          for (const FringePiece &piece : pieces) {
            if (SDL_strcmp(piece.digits, digits) != 0) {
              continue;
            }
            float art_top = (right_side ? piece.art_top_r : piece.art_top_l) * this->zoom;
            FringeDraw draw;
            draw.name = std::string(side) + digits;
            draw.rect = {left_x, cursor - art_top, 32.0f * this->zoom, 64.0f * this->zoom};
            fringe_draws.push_back(draw);
            cursor += (float) piece.left_steps * step_px;
            return;
          }
        };
        // A sloping high flank takes the top step, a sloping low flank the
        // bottom one, and the level part in between is bands. Both flanks can
        // slope at once — a quarter of the cliff edges in the shipped maps do
        // — and then both wedges are wanted, so the bands are whatever is left
        // over at the LEFT once the wedges have taken their share. Counting
        // them as the shallower of the two drops instead over-counted by one
        // wherever the two slopes cancelled, which is the case that leaves the
        // drop even but the face leaning.
        const char * high_wedge = high_tilt > 0 ? "0001" : (high_tilt < 0 ? "0010" : nullptr);
        const char * low_wedge = low_tilt > 0 ? "0110" : (low_tilt < 0 ? "1011" : nullptr);
        int wedge_left_steps = (high_tilt < 0 ? 1 : 0) + (low_tilt > 0 ? 1 : 0);
        int bands = SDL_max(left_steps - wedge_left_steps, 0);
        if (high_wedge != nullptr) {
          place(high_wedge);
        }
        for (int step = 0; step < bands; step++) {
          place("0011");
        }
        if (low_wedge != nullptr) {
          place(low_wedge);
        }
      }

      // Slope shading per corner, like the original's per vertex lit
      // terrain mesh: slopes shade smoothly across tile boundaries
      // instead of stepping per tile. The light comes from the rig the
      // original keeps in terrain/tilevar.cfg, which puts flat ground at
      // 0.979 of the texture color.
      float corner_bright[4];
      for (int i = 0; i < 4; i++) {
        int cx = (int) tile_x + corner_offsets[i][0];
        int cy = (int) tile_y + corner_offsets[i][1];
        auto clamped_h = [&](int hx, int hy) -> float {
          hx = std::clamp(hx, 0, (int) width);
          hy = std::clamp(hy, 0, (int) height);
          return this->cornerHeight((uint32_t) hx, (uint32_t) hy);
        };
        float gradient_x = (clamped_h(cx + 1, cy) - clamped_h(cx - 1, cy)) * 0.5f;
        float gradient_y = (clamped_h(cx, cy + 1) - clamped_h(cx, cy - 1)) * 0.5f;
        corner_bright[i] = this->terrainBrightness(gradient_x, gradient_y);
      }
      float brightness = (corner_bright[0] + corner_bright[1] + corner_bright[2] + corner_bright[3]) / 4.0f;

      // A type's art may span more than one tile, so the corners have to
      // be placed per type: an overlay splatting onto this tile carries
      // its own span. Within a span the tile takes its own window of the
      // texture, which keeps the art continuous across the block.
      auto textureCorners = [&](int for_type, SDL_FPoint * out) {
        auto found = this->terrain_texture_span.find(for_type);
        int span = found != this->terrain_texture_span.end() ? found->second : 1;
        if (span <= 1) {
          // Mirror the texture stamp per tile by a position hash so large
          // same type fields do not show a repeating grid
          uint32_t stamp = (tile_x * 73856093u) ^ (tile_y * 19349663u);
          float u0 = (stamp & 1) ? 1.0f : 0.0f;
          float v0 = (stamp & 2) ? 1.0f : 0.0f;
          out[0] = {u0, v0};
          out[1] = {1.0f - u0, v0};
          out[2] = {1.0f - u0, 1.0f - v0};
          out[3] = {u0, 1.0f - v0};
          return;
        }
        // Mirroring would break the seam between the tiles of a block, so
        // a spanning texture just runs on across them
        float window = 1.0f / (float) span;
        float u0 = (float) ((int) tile_x % span) * window;
        float v0 = (float) ((int) tile_y % span) * window;
        out[0] = {u0, v0};
        out[1] = {u0 + window, v0};
        out[2] = {u0 + window, v0 + window};
        out[3] = {u0, v0 + window};
      };
      SDL_FPoint texture_coordinates[4];
      textureCorners(this->terrain_textures.contains(tile.type) ? (int) tile.type : 0,
                     texture_coordinates);

      int type = this->terrain_textures.contains(tile.type) ? tile.type : 0;
      std::vector<SDL_Vertex> &vertices = batches[type];
      std::vector<int> &indices = batch_indices[type];
      int base = (int) vertices.size();
      for (int i = 0; i < 4; i++) {
        SDL_Vertex vertex;
        vertex.position = corners[i];
        vertex.color = {corner_bright[i], corner_bright[i], corner_bright[i], 1.0f};
        vertex.tex_coord = texture_coordinates[i];
        vertices.push_back(vertex);
      }
      indices.push_back(base);
      indices.push_back(base + 1);
      indices.push_back(base + 2);
      indices.push_back(base);
      indices.push_back(base + 2);
      indices.push_back(base + 3);

      // Where a different terrain type touches this tile — including
      // diagonally — its texture splats over it, weighted per corner by how
      // many of the four tiles meeting there carry that type. There is no
      // priority between types: the original mixes every neighbour in, which
      // is why a lone gray rock tile sitting in sand comes out half sand
      // rather than staying gray. Each overlay type is gathered once from all
      // eight neighbors. Concrete and asphalt carry blend=0 and take no part.
      const int around_offsets[8][2] = {{0, -1}, {1, -1}, {1, 0},  {1, 1},
                                        {0, 1},  {-1, 1}, {-1, 0}, {-1, -1}};
      int overlay_types[8];
      int overlay_count = 0;
      bool tile_blends = !this->unblended_terrain_types.contains((int) tile.type);
      for (int i = 0; i < 8 && tile_blends; i++) {
        int neighbor_type = typeAt((int) tile_x + around_offsets[i][0], (int) tile_y + around_offsets[i][1]);
        if (neighbor_type < 0 || neighbor_type == (int) tile.type ||
            !this->terrain_textures.contains(neighbor_type) ||
            this->unblended_terrain_types.contains(neighbor_type)) {
          continue;
        }
        bool seen = false;
        for (int j = 0; j < overlay_count; j++) {
          if (overlay_types[j] == neighbor_type) {
            seen = true;
            break;
          }
        }
        if (!seen) {
          overlay_types[overlay_count++] = neighbor_type;
        }
      }
      SDL_FPoint tile_center = {
        (corners[0].x + corners[1].x + corners[2].x + corners[3].x) / 4.0f,
        (corners[0].y + corners[1].y + corners[2].y + corners[3].y) / 4.0f,
      };
      for (int overlay = 0; overlay < overlay_count; overlay++) {
        int overlay_type = overlay_types[overlay];
        float corner_alpha[4];
        for (int i = 0; i < 4; i++) {
          int cx = (int) tile_x + corner_offsets[i][0];
          int cy = (int) tile_y + corner_offsets[i][1];
          corner_alpha[i] = (float) cornerTypeCount(cx, cy, overlay_type) / 4.0f;
        }
        // The overlay carries its own art, so it wants its own corners:
        // water spans two tiles where the ground under it spans one
        SDL_FPoint overlay_coordinates[4];
        textureCorners(overlay_type, overlay_coordinates);
        std::vector<SDL_Vertex> &blend_vertices = blend_batches[overlay_type];
        std::vector<int> &blend_index_list = blend_indices[overlay_type];
        int blend_base = (int) blend_vertices.size();
        for (int i = 0; i < 4; i++) {
          SDL_Vertex vertex;
          vertex.position = corners[i];
          vertex.color = {corner_bright[i], corner_bright[i], corner_bright[i], corner_alpha[i]};
          vertex.tex_coord = overlay_coordinates[i];
          blend_vertices.push_back(vertex);
        }
        // A center vertex keeps the interpolation symmetric across the
        // quad instead of leaning on one diagonal
        SDL_Vertex center_vertex;
        center_vertex.position = tile_center;
        // A tile keeps its own type at its centre, so no overlay reaches
        // there: the splat is strongest at the corners and fades inward.
        // That is what shades a lone tile from its own colour at the middle
        // out to a quarter of it at the corners.
        center_vertex.color = {brightness, brightness, brightness, 0.0f};
        center_vertex.tex_coord = {
          (overlay_coordinates[0].x + overlay_coordinates[2].x) / 2.0f,
          (overlay_coordinates[0].y + overlay_coordinates[2].y) / 2.0f,
        };
        blend_vertices.push_back(center_vertex);
        for (int i = 0; i < 4; i++) {
          blend_index_list.push_back(blend_base + i);
          blend_index_list.push_back(blend_base + ((i + 1) % 4));
          blend_index_list.push_back(blend_base + 4);
        }
      }
    }
  }

  for (auto &batch : cliff_batches) {
    SDL_RenderGeometry(renderer, this->terrain_textures[batch.first], batch.second.data(),
                       (int) batch.second.size(), cliff_indices[batch.first].data(),
                       (int) cliff_indices[batch.first].size());
  }
  for (const FringeDraw &draw : fringe_draws) {
    auto found = this->fringe_textures.find(draw.name);
    if (found == this->fringe_textures.end() || found->second == nullptr) {
      continue;
    }
    SDL_FRect rect = draw.rect;
    rect.x = SDL_roundf(rect.x);
    rect.y = SDL_roundf(rect.y);
    SDL_RenderTexture(renderer, found->second, NULL, &rect);
  }
  for (auto &batch : batches) {
    SDL_Texture * texture = this->terrain_textures.contains(batch.first) ? this->terrain_textures[batch.first] : nullptr;
    SDL_RenderGeometry(renderer, texture, batch.second.data(), (int) batch.second.size(),
                       batch_indices[batch.first].data(), (int) batch_indices[batch.first].size());
  }
  // Lower type numbers first so the higher, higher priority types land on
  // top where overlays stack. blend_batches is a std::map, so walking it
  // forward walks the types in order.
  for (auto &batch : blend_batches) {
    SDL_RenderGeometry(renderer, this->terrain_textures[batch.first], batch.second.data(),
                       (int) batch.second.size(), blend_indices[batch.first].data(),
                       (int) blend_indices[batch.first].size());
  }

  this->drawObjects(renderer, window_rect, center_x, center_y);
}

// The water column seen through a glass tank wall: the water archive's
// side views, one 32 pixel half edge segment per band of two steps from
// the wall's base up to the surface, with the grey glass pane blended
// over them. Only walls rising above the ground on their outer side show
// glass; a dug pool's submerged lining stays plain under its surface
// like the original. Faces whose outer flank sits nearer the camera are
// the visible ones.
void MapView::drawTankWallFace(SDL_Renderer * renderer, const ZooObject * object, float center_x, float center_y) {
  float tile_x = SDL_roundf((float) object->x / 64.0f * 2.0f) / 2.0f;
  float tile_y = SDL_roundf((float) object->y / 64.0f * 2.0f) / 2.0f;
  bool y_run = tile_x == SDL_floorf(tile_x);
  int ax;
  int ay;
  int bx;
  int by;
  if (y_run) {
    ax = (int) tile_x - 1;
    ay = (int) SDL_floorf(tile_y);
    bx = (int) tile_x;
    by = ay;
  } else {
    ax = (int) SDL_floorf(tile_x);
    ay = (int) tile_y - 1;
    bx = ax;
    by = (int) tile_y;
  }
  float unused;
  float depth_a;
  float depth_b;
  this->tileToWorld((float) ax + 0.5f, (float) ay + 0.5f, &unused, &depth_a);
  this->tileToWorld((float) bx + 0.5f, (float) by + 0.5f, &unused, &depth_b);
  int far_x = depth_a <= depth_b ? ax : bx;
  int far_y = depth_a <= depth_b ? ay : by;
  int near_x = depth_a <= depth_b ? bx : ax;
  int near_y = depth_a <= depth_b ? by : ay;
  auto tank = this->tank_water_tiles.find(((uint64_t) (uint32_t) far_x << 32) | (uint32_t) far_y);
  if (tank == this->tank_water_tiles.end()) {
    return;
  }
  if (this->tank_water_tiles.contains(((uint64_t) (uint32_t) near_x << 32) | (uint32_t) near_y)) {
    return;
  }
  if (near_x < 0 || near_y < 0 || near_x >= (int) this->zoo->getWidth() || near_y >= (int) this->zoo->getHeight()) {
    return;
  }
  float base = (float) object->elevation / 16.0f;
  float outside = (float) (int32_t) this->zoo->getTile((uint32_t) near_x, (uint32_t) near_y).height;
  if (base + TANK_WALL_STEPS <= outside) {
    return;
  }
  // The face normal points from the tank to the outside; a rightward one
  // on screen shows the right view, a leftward one the front view
  float far_world_x;
  float near_world_x;
  this->tileToWorld((float) far_x + 0.5f, (float) far_y + 0.5f, &far_world_x, &unused);
  this->tileToWorld((float) near_x + 0.5f, (float) near_y + 0.5f, &near_world_x, &unused);
  bool right_face = near_world_x > far_world_x;
  Animation * water = this->resource_manager->getAnimation(right_face ? "water/salt/right/right" : "water/salt/front/front");
  Animation * glass = this->resource_manager->getAnimation(right_face ? "water/glass/rgrey/rgrey" : "water/glass/fgrey/fgrey");
  if (water == nullptr) {
    return;
  }
  float level = tank->second;
  // Each face piece is one step of height (its 32 pixel box is the step
  // plus the diamond slant); the ground outside hides everything below
  // itself, so the column starts there
  float start = SDL_max(base, outside);
  int bands = (int) SDL_floorf(level - start);
  for (int segment = 0; segment < 2; segment++) {
    float segment_x;
    float segment_y;
    if (y_run) {
      segment_x = tile_x;
      segment_y = (float) ay + 0.25f + 0.5f * (float) segment;
    } else {
      segment_x = (float) ax + 0.25f + 0.5f * (float) segment;
      segment_y = tile_y;
    }
    float world_x;
    float world_y;
    this->tileToWorld(segment_x, segment_y, &world_x, &world_y);
    float screen_x = (world_x - this->camera_x) * this->zoom + center_x;
    for (int band = 0; band < bands; band++) {
      float band_y = world_y - (start + (float) band) * HEIGHT_STEP;
      float screen_y = (band_y - this->camera_y) * this->zoom + center_y;
      float box_x0 = 0.0f;
      float box_y0 = 0.0f;
      float box_width = 0.0f;
      float box_height = 0.0f;
      if (!water->getBox(&box_x0, &box_y0, &box_width, &box_height)) {
        return;
      }
      SDL_FRect destination = {
        (float) SDL_lroundf(screen_x + box_x0 * this->zoom),
        (float) SDL_lroundf(screen_y + box_y0 * this->zoom),
        (float) SDL_lroundf(box_width * this->zoom),
        (float) SDL_lroundf(box_height * this->zoom),
      };
      // The tank's contents show through the face in the original, so it
      // blends at half like the surface does
      water->drawByKey(renderer, &destination, "N", false, 128);
      if (glass != nullptr) {
        glass->drawByKey(renderer, &destination, "N", false, 128);
      }
    }
  }
}

// One tile diamond of the tank water surface at its tank's level, drawn
// translucent like the original's translucent water so the basin floor
// and the submerged walls show through. Tanks aligned to full tiles tile
// the surface with top alone; the half diamond tope/topn/tops/topw
// fringe pieces are for half tile aligned tanks, which med_kids does not
// have.
void MapView::drawWaterTile(SDL_Renderer * renderer, const WaterDraw &draw, float center_x, float center_y) {
  Animation * top = this->resource_manager->getAnimation("water/salt/top/top");
  if (top == nullptr) {
    return;
  }
  float world_x;
  float world_y;
  this->tileToWorld(draw.tile_x + 0.5f, draw.tile_y + 0.5f, &world_x, &world_y);
  world_y -= draw.level * HEIGHT_STEP;
  float screen_x = (world_x - this->camera_x) * this->zoom + center_x;
  float screen_y = (world_y - this->camera_y) * this->zoom + center_y;
  float box_x0 = 0.0f;
  float box_y0 = 0.0f;
  float box_width = 0.0f;
  float box_height = 0.0f;
  if (!top->getBox(&box_x0, &box_y0, &box_width, &box_height)) {
    return;
  }
  SDL_FRect destination = {
    (float) SDL_lroundf(screen_x + box_x0 * this->zoom),
    (float) SDL_lroundf(screen_y + box_y0 * this->zoom),
    (float) SDL_lroundf(box_width * this->zoom),
    (float) SDL_lroundf(box_height * this->zoom),
  };
  top->drawByKey(renderer, &destination, "N", false, 128);
}

// The rotation field steps in increments of 2 per quarter turn, clockwise
// with rotation 0 facing NW: rotation 6 draws the SW view at the default
// camera (orientation 1), verified against the entrance arch, gate booths
// and hedges of fshore.zoo. Each camera quarter turn moves one more step.
std::string MapView::rotationDirection(uint32_t rotation) {
  const char * directions[4] = {"SE", "SW", "NW", "NE"};
  return directions[((rotation / 2) + 7 - (uint32_t) this->orientation) % 4];
}

// The horizontally flipped counterpart of a direction key. Most entity art
// only ships the east side views and the west side ones draw mirrored.
static std::string mirrorDirectionKey(const std::string &key) {
  if (key == "SW") return "SE";
  if (key == "SE") return "SW";
  if (key == "W") return "E";
  if (key == "E") return "W";
  if (key == "NW") return "NE";
  if (key == "NE") return "NW";
  return "";
}

void MapView::loadObjectRegistry() {
  this->registry_loaded = true;
  std::vector<std::string> config_files = this->resource_manager->getResourceNamesWithExtension("CFG");
  std::sort(config_files.begin(), config_files.end());
  for (const std::string &config_file : config_files) {
    IniReader * reader = this->resource_manager->getIniReader(config_file);
    if (reader != nullptr) {
      this->registry_configs.push_back(reader);
    }
  }
}

std::string MapView::registryLookup(const std::string &section, const std::string &key) {
  for (IniReader * reader : this->registry_configs) {
    std::string value = reader->get(section, key);
    if (!value.empty()) {
      return value;
    }
  }
  return "";
}

// Finds where an object's art lives through the cfg registry. Leaf entries
// ([subcategory] code = leaf.ai) describe one object and group entries
// ([category] subcategory = group.ai) bundle pieces like tank walls and
// gates, keyed by the piece code. The ai file names its animations with
// either a full path or a bare state name relative to the directory its
// icon art lives in.
std::string MapView::objectArtPath(const ZooObject * object) {
  if (!this->registry_loaded) {
    this->loadObjectRegistry();
  }
  std::string ai_path = this->registryLookup(object->subcategory, object->code);
  bool group = false;
  if (ai_path.empty()) {
    ai_path = this->registryLookup(object->category, object->subcategory);
    group = true;
  }
  if (ai_path.empty()) {
    return "";
  }
  IniReader * ai_reader = this->resource_manager->getIniReader(ai_path);
  if (ai_reader == nullptr) {
    return "";
  }
  // Idle is the resting state, food pieces only have their fill states
  std::string animation_section = group ? object->code + "/Animations" : "Animations";
  std::string value;
  for (const char * state : {"idle", "full", "mid", "small"}) {
    value = ai_reader->get(animation_section, state);
    if (value.empty() && group) {
      value = ai_reader->get("Animations", state);
    }
    if (!value.empty()) {
      break;
    }
  }
  std::string result;
  if (value.find('/') != std::string::npos) {
    if (value.ends_with(".ani")) {
      value = value.substr(0, value.length() - 4);
    }
    result = value;
  } else if (!value.empty()) {
    std::vector<std::string> icons = ai_reader->getList(group ? object->code + "/Icon" : "Icon", "Icon");
    if (icons.empty()) {
      icons = ai_reader->getList("Icon", "Icon");
    }
    if (!icons.empty()) {
      // The icon path is <art directory>/<icon animation>/<icon name>
      std::string base = icons.front();
      for (int i = 0; i < 2; i++) {
        size_t cut = base.rfind('/');
        if (cut != std::string::npos) {
          base = base.substr(0, cut);
        }
      }
      if (group) {
        base += "/" + object->code;
      }
      result = base + "/" + value + "/" + value;
    }
  }
  delete ai_reader;
  return result;
}

// Buildings with cIsColorReplaced art keep 16 recolorable entries at the
// top of their pallet, after the [cr_color] ncolors fixed ones. The ai's
// [colorrep] block names the default replacement ramp; without it the base
// art draws in the neutral gray it ships with, which the original never
// shows (verified against med_kids' sub shop, lime by default).
std::pair<std::string, int> MapView::objectColorRep(const ZooObject * object) {
  if (this->color_replacements.contains(object->code)) {
    return this->color_replacements[object->code];
  }
  std::pair<std::string, int> result = {"", 0};
  if (!this->registry_loaded) {
    this->loadObjectRegistry();
  }
  std::string ai_path = this->registryLookup(object->subcategory, object->code);
  if (ai_path.empty()) {
    ai_path = this->registryLookup(object->category, object->subcategory);
  }
  if (!ai_path.empty()) {
    IniReader * ai_reader = this->resource_manager->getIniReader(ai_path);
    if (ai_reader != nullptr) {
      std::string default_pallet = ai_reader->get("colorrep", "defaultpal");
      int fixed_colors = ai_reader->getInt("cr_color", "ncolors");
      if (!default_pallet.empty() && fixed_colors > 0) {
        result = {default_pallet, fixed_colors};
      }
      delete ai_reader;
    }
  }
  this->color_replacements[object->code] = result;
  return result;
}

// Art locations differ per category: plain objects have an idle animation
// under objects/<code>, fences have one animation per direction and paths
// have numbered shape pieces picked by which neighbors are also paths
Animation * MapView::objectAnimation(const ZooObject * object, std::string &draw_key) {
  std::string cache_key;
  std::string animation_path;
  if (object->category == "fences") {
    // Fence pieces sit on tile edges at half tile positions: 0 is the NE
    // facing piece of an x axis run, 6 the SE facing piece of a y axis
    // run. On a sloped edge the piece uses its 30 degree slope art. The
    // art's positive direction runs with +y on y runs but against +x on
    // x runs (verified against the original on fshore's y run wall and
    // deathmtn's x run wall).
    draw_key = rotationDirection(object->rotation);
    std::string state = "idle";
    float edge_start = 0.0f;
    float edge_end = 0.0f;
    bool y_run = false;
    this->fenceEdgeHeights(object, &edge_start, &edge_end, &y_run);
    // The original slopes the piece exactly where a flanking tile sees
    // the edge sloping one step (fenceEdgeHeights reports that flank's
    // corner pair); hard cliffs with flat flanks stay flat even though
    // the anchor is fractional (verified on deathmtn's cliff base and
    // the fencetest terraces). The p/n labels of the art are relative to
    // the drawn face: the SW and NW views ship them swapped (their tall
    // and short frames trade places), so back facing pieces invert the
    // rule — med_kids' stair gate stub was the giveaway.
    float edge_span = edge_end - edge_start;
    bool inverted_key = draw_key == "SW" || draw_key == "NW";
    if (edge_span == 1.0f) {
      state = (y_run != inverted_key) ? "idle30p" : "idle30n";
    } else if (edge_span == -1.0f) {
      state = (y_run != inverted_key) ? "idle30n" : "idle30p";
    }
    if (SDL_getenv("OPENZTC_DEBUG_SORT") != nullptr) {
      SDL_Log("fence (%.2f, %.2f) %s edge %.1f -> %.1f state %s key %s",
              (float) object->x / 64.0f, (float) object->y / 64.0f, y_run ? "y-run" : "x-run",
              edge_start, edge_end, state.c_str(), draw_key.c_str());
    }
    animation_path = "fences/" + object->subcategory + "/" + object->code + "/" + state + "/" + state;
    cache_key = animation_path;
  } else if (object->category == "paths") {
    // Piece 5 is the isolated piece, the 16 neighbor combinations follow
    // it. The mask bits are in screen directions, so each neighbor's bit
    // depends on where the map orientation puts it on screen. The bit per
    // screen direction in the SE, SW, NW, NE ring, read off the border
    // sides of the piece art itself (straight runs are invariant under
    // swapping SW and NE, which is why the old {2,4,8,1} table survived
    // every straight-path comparison and only corner and junction pieces
    // drew a border facing their neighbor):
    static const int screen_direction_bit[4] = {2, 1, 8, 4};
    uint32_t tile_x = object->x / 64;
    uint32_t tile_y = object->y / 64;
    // Sloped tiles use the four ramp pieces 1 to 4 instead of the mask
    // ones, picked by which way the tile rises on screen
    if (tile_x < this->zoo->getWidth() && tile_y < this->zoo->getHeight()) {
      float nw = this->tileCornerHeight(tile_x, tile_y, 0);
      float ne = this->tileCornerHeight(tile_x, tile_y, 1);
      float se = this->tileCornerHeight(tile_x, tile_y, 2);
      float sw = this->tileCornerHeight(tile_x, tile_y, 3);
      if (!(nw == ne && ne == se && se == sw)) {
        // The rising direction in the same slots the neighbor bits use:
        // x+1 is 0, y+1 is 1, x-1 is 2, y-1 is 3
        int rising;
        if (sw + se > nw + ne) {
          rising = 1;
        } else if (nw + ne > sw + se) {
          rising = 3;
        } else if (ne + se > nw + sw) {
          rising = 0;
        } else {
          rising = 2;
        }
        draw_key = std::to_string(1 + (rising + 4 - (int) this->orientation) % 4);
      }
    }
    if (draw_key.empty()) {
      int mask = 0;
      if (tile_y > 0 && this->path_tiles.contains(((uint64_t) tile_x << 32) | (tile_y - 1))) {
        mask |= screen_direction_bit[(3 + 4 - this->orientation) % 4];
      }
      if (this->path_tiles.contains(((uint64_t) (tile_x + 1) << 32) | tile_y)) {
        mask |= screen_direction_bit[(0 + 4 - this->orientation) % 4];
      }
      if (this->path_tiles.contains(((uint64_t) tile_x << 32) | (tile_y + 1))) {
        mask |= screen_direction_bit[(1 + 4 - this->orientation) % 4];
      }
      if (tile_x > 0 && this->path_tiles.contains(((uint64_t) (tile_x - 1) << 32) | tile_y)) {
        mask |= screen_direction_bit[(2 + 4 - this->orientation) % 4];
      }
      draw_key = std::to_string(5 + mask);
    }
    if (SDL_getenv("OPENZTC_DEBUG_PATH") != nullptr) {
      SDL_Log("path %s (%u, %u) piece %s", object->code.c_str(), tile_x, tile_y, draw_key.c_str());
    }
    animation_path = "paths/" + object->code + "/idle/idle";
    cache_key = animation_path;
  } else if (object->category == "ambient" || object->category == "helicopter") {
    // Ambient markers are sound emitters without art
    return nullptr;
  } else if (object->category == "animals" || object->category == "guests" ||
             object->category == "keeper" || object->category == "maint" ||
             object->category == "tour") {
    // Entities stand still until the simulation drives them. Records whose
    // position could not be decoded stay hidden instead of piling up in a
    // map corner.
    if (object->x == 0 && object->y == 0) {
      return nullptr;
    }
    if (object->category == "animals" && !this->sim_animals.empty()) {
      // The simulation drives the animals from these records
      return nullptr;
    }
    if (object->category == "animals") {
      // Animals in water swim; aquatic species have no stand art at all
      // and fall back to their surface swim
      static const char * entity_directions_animal[8] = {"SE", "S", "SW", "W", "NW", "N", "NE", "E"};
      draw_key = entity_directions_animal[(object->rotation + 14 - 2 * (uint32_t) this->orientation) % 8];
      std::string base = "animals/" + object->subcategory + "/" + object->code + "/";
      bool in_water = this->isWaterAt((int32_t) object->x, (int32_t) object->y);
      std::vector<std::string> candidates;
      if (in_water) {
        candidates = {base + "swim/swim", base + "surfswim/surfswim", base + "stand/stand"};
      } else {
        candidates = {base + "stand/stand", base + "surfswim/surfswim"};
      }
      return this->firstAnimation(candidates, base + (in_water ? "#water" : "#land"));
    } else if (object->category == "guests") {
      // The layered sprite art, one set per guest type
      std::string body = "lsmguest";
      if (object->subcategory == "woman") {
        body = "lsfguest";
      } else if (object->subcategory == "boy") {
        body = "lsbguest";
      } else if (object->subcategory == "girl") {
        body = "lsgguest";
      }
      animation_path = "guests/" + body + "/" + body;
    } else {
      std::string kind = object->category == "keeper" ? "keepr" : object->category == "maint" ? "maint" : "guide";
      std::string gender = object->code == "f" ? "f" : "m";
      animation_path = "staff/ls" + gender + kind + "/ls" + gender + kind;
    }
    // Entities face eight directions clockwise from NW — the same base as
    // the object ring, verified against the original on a fresh save's
    // polar bears (facing 6 shows the SW view, 7 the W view)
    static const char * entity_directions[8] = {"SE", "S", "SW", "W", "NW", "N", "NE", "E"};
    draw_key = entity_directions[(object->rotation + 14 - 2 * (uint32_t) this->orientation) % 8];
    cache_key = animation_path;
  } else {
    // Tank walls carry a piece code like fences do, plain objects face
    // where their rotation points
    draw_key = rotationDirection(object->rotation);
    // The usual layout is an idle animation under objects/<code>. The cfg
    // registry locates the art that deviates from it, like tank pieces and
    // effects with redirected animations, but its icon derived locations
    // are not always right (fgate1's icon points at fgate's art), so the
    // convention comes first. Food pieces only have their fill states and
    // some art ships one animation file per direction.
    std::vector<std::string> candidates;
    candidates.push_back("objects/" + object->code + "/idle/idle");
    std::string registry_path = this->objectArtPath(object);
    if (!registry_path.empty()) {
      candidates.push_back(registry_path);
    }
    candidates.push_back(object->category + "/" + object->code + "/full/full");
    if (!draw_key.empty()) {
      candidates.push_back("objects/" + object->code + "/" + draw_key + "/" + draw_key);
    }
    cache_key = object->category + "/" + object->subcategory + "/" + object->code + ":" + draw_key;
    if (this->object_animations.contains(cache_key)) {
      return this->object_animations[cache_key];
    }
    std::pair<std::string, int> color_replacement = this->objectColorRep(object);
    Animation * found = nullptr;
    for (const std::string &candidate : candidates) {
      found = this->resource_manager->getAnimation(candidate, color_replacement.first, color_replacement.second);
      if (found != nullptr) {
        break;
      }
    }
    this->object_animations[cache_key] = found;
    if (found == nullptr) {
      this->missing_object_art++;
    }
    return found;
  }
  if (this->object_animations.contains(cache_key)) {
    return this->object_animations[cache_key];
  }
  Animation * animation = this->resource_manager->getAnimation(animation_path);
  this->object_animations[cache_key] = animation;
  if (animation == nullptr) {
    this->missing_object_art++;
  }
  return animation;
}

void MapView::drawObjects(SDL_Renderer * renderer, SDL_FRect * window_rect, float center_x, float center_y) {
  // The tank water surface interleaves with the painter order: each tile's
  // diamond draws in its own row, over the tile's objects and its near
  // wall (below the surface in a dug tank) and under everything in front
  size_t water_index = 0;
  for (size_t object_index = 0; object_index < this->sorted_objects.size(); object_index++) {
    const ZooObject * object = this->sorted_objects[object_index];
    while (water_index < this->water_draws.size() &&
           this->water_draws[water_index].key <= this->sorted_object_keys[object_index]) {
      this->drawWaterTile(renderer, this->water_draws[water_index], center_x, center_y);
      water_index++;
    }
    // Positions are in 64ths of a tile
    float tile_x = (float) object->x / 64.0f;
    float tile_y = (float) object->y / 64.0f;
    if (object->category == "fences" || object->category == "tankwall") {
      // Fence positions sit just off the edge midpoint (63/64 fractions
      // encode the facing side); anchor at the exact half tile point so
      // neighboring pieces land on the same pixel grid
      tile_x = SDL_roundf(tile_x * 2.0f) / 2.0f;
      tile_y = SDL_roundf(tile_y * 2.0f) / 2.0f;
    }
    float world_x;
    float world_y;
    this->tileToWorld(tile_x, tile_y, &world_x, &world_y);
    // Anchor the sprite at the elevation the original computed when the
    // object was placed, stored in its record in 16ths of a height step.
    // That is the original's own answer for slopes and cliffs — deriving
    // it from the terrain (corner claims, interpolation) drifted from the
    // real game wherever tiles disagree about an edge. Fence pieces get
    // no extra face fill: the original draws nothing below a piece (the
    // art's own skirt and the terrain cliff face cover drops, verified on
    // the fencetest terraces).
    world_y -= (float) object->elevation / 16.0f * HEIGHT_STEP;
    float screen_x = (world_x - this->camera_x) * this->zoom + center_x;
    float screen_y = (world_y - this->camera_y) * this->zoom + center_y;
    if (screen_x < -200.0f || screen_x > window_rect->w + 200.0f ||
        screen_y < -200.0f || screen_y > window_rect->h + 200.0f) {
      continue;
    }
    if (SDL_getenv("OPENZTC_DEBUG_ANCHOR") != nullptr) {
      SDL_Log("anchor %s/%s (%.2f, %.2f) elev %d screen (%.1f, %.1f)",
              object->category.c_str(), object->code.c_str(), tile_x, tile_y,
              object->elevation, screen_x, screen_y);
    }
    std::string draw_key;
    Animation * animation = this->objectAnimation(object, draw_key);
    if (animation == nullptr) {
      continue;
    }
    // Art without the wanted direction draws the mirrored counterpart
    // flipped when one exists (entity art usually only ships the east
    // side views); otherwise, like single state food pieces, it draws
    // its default animation instead
    bool mirrored = false;
    if (!draw_key.empty()) {
      float key_width = 0.0f;
      float key_height = 0.0f;
      if (!animation->getSizeByKey(draw_key, &key_width, &key_height)) {
        std::string mirror_key = mirrorDirectionKey(draw_key);
        if (!mirror_key.empty() && animation->getSizeByKey(mirror_key, &key_width, &key_height)) {
          draw_key = mirror_key;
          mirrored = true;
        } else {
          draw_key.clear();
        }
      }
    }
    // Anchor the sprite by the box its ani file declares around the world
    // anchor point, falling back to bottom center for art without one.
    // The frames sit inside that box by their own offsets.
    float box_x0 = 0.0f;
    float box_y0 = 0.0f;
    float sprite_width = 0.0f;
    float sprite_height = 0.0f;
    if (!animation->getBox(&box_x0, &box_y0, &sprite_width, &sprite_height)) {
      if (!draw_key.empty()) {
        if (!animation->getSizeByKey(draw_key, &sprite_width, &sprite_height)) {
          continue;
        }
      } else if (!animation->getSize(&sprite_width, &sprite_height)) {
        continue;
      }
      box_x0 = -sprite_width / 2.0f;
      box_y0 = -sprite_height;
    }
    // Mirroring flips the box around the anchor point too. The corners
    // snap to whole pixels like the original's software blitter; on a
    // fractional position the linear texture filter would smear the
    // sprite edges, drawing a seam at every fence piece boundary.
    float destination_x0 = mirrored ? -(box_x0 + sprite_width) : box_x0;
    SDL_FRect destination = {
      (float) SDL_lroundf(screen_x + destination_x0 * this->zoom),
      (float) SDL_lroundf(screen_y + box_y0 * this->zoom),
      (float) SDL_lroundf(sprite_width * this->zoom),
      (float) SDL_lroundf(sprite_height * this->zoom),
    };
    if (!draw_key.empty()) {
      animation->drawByKey(renderer, &destination, draw_key, mirrored);
    } else {
      animation->draw(renderer, &destination);
    }
    if (object->category == "tankwall" && !this->tank_water_tiles.empty()) {
      this->drawTankWallFace(renderer, object, center_x, center_y);
    }
  }
  while (water_index < this->water_draws.size()) {
    this->drawWaterTile(renderer, this->water_draws[water_index], center_x, center_y);
    water_index++;
  }
  this->drawSimGuests(renderer, center_x, center_y);
}

// One simulation entity, anchored like the static objects are
void MapView::drawSimEntity(SDL_Renderer * renderer, float center_x, float center_y,
                            const std::string &animation_path, int32_t x, int32_t y, uint8_t facing) {
  static const char * entity_directions[8] = {"SE", "S", "SW", "W", "NW", "N", "NE", "E"};
  Animation * animation = nullptr;
  if (this->object_animations.contains(animation_path)) {
    animation = this->object_animations[animation_path];
  } else {
    animation = this->resource_manager->getAnimation(animation_path);
    this->object_animations[animation_path] = animation;
  }
  if (animation == nullptr) {
    return;
  }
  float tile_x = (float) x / 64.0f;
  float tile_y = (float) y / 64.0f;
  float world_x;
  float world_y;
  this->tileToWorld(tile_x, tile_y, &world_x, &world_y);
  world_y -= this->heightAt(tile_x, tile_y) * HEIGHT_STEP;
  float screen_x = (world_x - this->camera_x) * this->zoom + center_x;
  float screen_y = (world_y - this->camera_y) * this->zoom + center_y;
  std::string draw_key = entity_directions[(facing + 14 - 2 * (uint32_t) this->orientation) % 8];
  float box_x0 = 0.0f;
  float box_y0 = 0.0f;
  float sprite_width = 0.0f;
  float sprite_height = 0.0f;
  if (!animation->getBox(&box_x0, &box_y0, &sprite_width, &sprite_height)) {
    if (!animation->getSize(&sprite_width, &sprite_height)) {
      return;
    }
    box_x0 = -sprite_width / 2.0f;
    box_y0 = -sprite_height;
  }
  float key_width = 0.0f;
  float key_height = 0.0f;
  bool mirrored = false;
  if (!animation->getSizeByKey(draw_key, &key_width, &key_height)) {
    std::string mirror_key = mirrorDirectionKey(draw_key);
    if (!mirror_key.empty() && animation->getSizeByKey(mirror_key, &key_width, &key_height)) {
      draw_key = mirror_key;
      mirrored = true;
    } else {
      draw_key.clear();
    }
  }
  float destination_x0 = mirrored ? -(box_x0 + sprite_width) : box_x0;
  SDL_FRect destination = {
    screen_x + destination_x0 * this->zoom,
    screen_y + box_y0 * this->zoom,
    sprite_width * this->zoom,
    sprite_height * this->zoom,
  };
  if (!draw_key.empty()) {
    animation->drawByKey(renderer, &destination, draw_key, mirrored);
  } else {
    animation->draw(renderer, &destination);
  }
}

// The simulation's entities draw after the static objects. They can clip
// behind foreground scenery until they join the painter order properly.
void MapView::drawSimGuests(SDL_Renderer * renderer, float center_x, float center_y) {
  static const char * guest_bodies[4] = {"lsmguest", "lsfguest", "lsbguest", "lsgguest"};
  for (const SimGuest &guest : this->sim_guests) {
    std::string body = guest_bodies[guest.type % 4];
    this->drawSimEntity(renderer, center_x, center_y, "guests/" + body + "/" + body, guest.x, guest.y, guest.facing);
  }
  for (const SimAnimal &animal : this->sim_animals) {
    // Walking animals use their walk art, resting ones stand; animals in
    // water swim either way, and aquatic species without land art fall
    // back to their surface swim
    std::string base = "animals/" + animal.species + "/" + animal.sex + "/";
    bool resting = animal.x == animal.target_x && animal.y == animal.target_y;
    bool in_water = this->isWaterAt(animal.x, animal.y);
    std::vector<std::string> candidates;
    if (in_water) {
      candidates = {base + "swim/swim", base + "surfswim/surfswim",
                    base + (resting ? "stand/stand" : "walk/walk")};
    } else {
      candidates = {base + (resting ? "stand/stand" : "walk/walk"), base + "surfswim/surfswim"};
    }
    std::string cache_key = base + (in_water ? "#simwater" : (resting ? "#simstand" : "#simwalk"));
    Animation * animation = this->firstAnimation(candidates, cache_key);
    if (animation == nullptr) {
      continue;
    }
    this->drawSimEntity(renderer, center_x, center_y, cache_key, animal.x, animal.y, animal.facing);
  }
}
