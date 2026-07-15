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
#define HEIGHT_STEP 12.0f

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

// Paint order: back to front by screen depth
void MapView::sortObjects() {
  std::sort(this->sorted_objects.begin(), this->sorted_objects.end(),
            [this](const ZooObject * a, const ZooObject * b) {
              float world_x;
              float depth_a;
              float depth_b;
              this->tileToWorld((float) a->x / 64.0f, (float) a->y / 64.0f, &world_x, &depth_a);
              this->tileToWorld((float) b->x / 64.0f, (float) b->y / 64.0f, &world_x, &depth_b);
              return depth_a < depth_b;
            });
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
// start is the corner with the smaller coordinate along the run.
void MapView::fenceEdgeHeights(const ZooObject * object, float * start, float * end) {
  *start = 0.0f;
  *end = 0.0f;
  float fraction_x = (float) (object->x % 64) / 64.0f;
  bool y_run = fraction_x <= 0.25f || fraction_x >= 0.75f;
  uint32_t edge_x;
  uint32_t edge_y;
  if (y_run) {
    edge_x = (uint32_t) (((float) object->x / 64.0f) + 0.5f);
    edge_y = object->y / 64;
    if (edge_x <= this->zoo->getWidth() && edge_y + 1 <= this->zoo->getHeight()) {
      *start = this->cornerHeight(edge_x, edge_y);
      *end = this->cornerHeight(edge_x, edge_y + 1);
    }
  } else {
    edge_x = object->x / 64;
    edge_y = (uint32_t) (((float) object->y / 64.0f) + 0.5f);
    if (edge_x + 1 <= this->zoo->getWidth() && edge_y <= this->zoo->getHeight()) {
      *start = this->cornerHeight(edge_x, edge_y);
      *end = this->cornerHeight(edge_x + 1, edge_y);
    }
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

void MapView::loadTerrainTextures(SDL_Renderer * renderer) {
  this->textures_loaded = true;
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
      if (type < 0 || texture_name.empty() || this->terrain_textures.contains(type)) {
        continue;
      }
      SDL_Texture * texture = this->resource_manager->getTexture(renderer, texture_name);
      if (texture != nullptr) {
        // Boundary blending draws neighbor textures with vertex alpha
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        this->terrain_textures[type] = texture;
      }
    }
    delete reader;
  }
  SDL_Log("Loaded %i terrain textures", (int) this->terrain_textures.size());
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

  // The tile type at a position, or -1 off the map
  auto typeAt = [&](int x, int y) -> int {
    if (x < 0 || y < 0 || x >= (int) width || y >= (int) height) {
      return -1;
    }
    return (int) this->zoo->getTile(x, y).type;
  };
  // Whether any of the four tiles meeting at grid corner (cx, cy) has the
  // wanted type
  auto cornerTouchesType = [&](int cx, int cy, int wanted) -> bool {
    return typeAt(cx - 1, cy - 1) == wanted || typeAt(cx, cy - 1) == wanted ||
           typeAt(cx - 1, cy) == wanted || typeAt(cx, cy) == wanted;
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
        float cliff_shade = 0.6f;
        SDL_FColor cliff_color = {cliff_shade, cliff_shade, cliff_shade, 1.0f};
        SDL_FPoint cliff_positions[4] = {{ax, ay_high}, {bx, by_high}, {bx, by_low}, {ax, ay_low}};
        const SDL_FPoint cliff_uv[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        std::vector<SDL_Vertex> &cliff_vertices = cliff_batches[cliff_type];
        std::vector<int> &cliff_index_list = cliff_indices[cliff_type];
        int cliff_base = (int) cliff_vertices.size();
        for (int i = 0; i < 4; i++) {
          SDL_Vertex vertex;
          vertex.position = cliff_positions[i];
          vertex.color = cliff_color;
          vertex.tex_coord = cliff_uv[i];
          cliff_vertices.push_back(vertex);
        }
        cliff_index_list.push_back(cliff_base);
        cliff_index_list.push_back(cliff_base + 1);
        cliff_index_list.push_back(cliff_base + 2);
        cliff_index_list.push_back(cliff_base);
        cliff_index_list.push_back(cliff_base + 2);
        cliff_index_list.push_back(cliff_base + 3);
      }

      // Slope shading per corner, like the original's per vertex lit
      // terrain mesh: slopes shade smoothly across tile boundaries
      // instead of stepping per tile. Flat terrain renders at roughly
      // 0.95 of the texture color (measured on fshore.zoo water against
      // depwater.tga).
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
        corner_bright[i] = std::clamp(0.95f - 0.15f * (gradient_x + gradient_y), 0.4f, 1.0f);
      }
      float brightness = (corner_bright[0] + corner_bright[1] + corner_bright[2] + corner_bright[3]) / 4.0f;

      // Mirror the texture stamp per tile by a position hash so large
      // same type fields do not show a repeating grid
      uint32_t stamp = (tile_x * 73856093u) ^ (tile_y * 19349663u);
      float u0 = (stamp & 1) ? 1.0f : 0.0f;
      float v0 = (stamp & 2) ? 1.0f : 0.0f;
      const SDL_FPoint texture_coordinates[4] = {
        {u0, v0}, {1.0f - u0, v0}, {1.0f - u0, 1.0f - v0}, {u0, 1.0f - v0}};

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
      // diagonally — its texture splats across the whole tile, full at the
      // corners that touch it and fading to nothing at the ones that do
      // not, the way the original's blended terrain mesh looks. Lower type
      // numbers paint over higher ones, so each overlay type is gathered
      // once from all eight neighbors.
      const int around_offsets[8][2] = {{0, -1}, {1, -1}, {1, 0},  {1, 1},
                                        {0, 1},  {-1, 1}, {-1, 0}, {-1, -1}};
      int overlay_types[8];
      int overlay_count = 0;
      for (int i = 0; i < 8; i++) {
        int neighbor_type = typeAt((int) tile_x + around_offsets[i][0], (int) tile_y + around_offsets[i][1]);
        if (neighbor_type < 0 || neighbor_type >= (int) tile.type ||
            !this->terrain_textures.contains(neighbor_type)) {
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
        float alpha_sum = 0.0f;
        for (int i = 0; i < 4; i++) {
          int cx = (int) tile_x + corner_offsets[i][0];
          int cy = (int) tile_y + corner_offsets[i][1];
          corner_alpha[i] = cornerTouchesType(cx, cy, overlay_type) ? 1.0f : 0.0f;
          alpha_sum += corner_alpha[i];
        }
        std::vector<SDL_Vertex> &blend_vertices = blend_batches[overlay_type];
        std::vector<int> &blend_index_list = blend_indices[overlay_type];
        int blend_base = (int) blend_vertices.size();
        for (int i = 0; i < 4; i++) {
          SDL_Vertex vertex;
          vertex.position = corners[i];
          vertex.color = {corner_bright[i], corner_bright[i], corner_bright[i], corner_alpha[i]};
          vertex.tex_coord = texture_coordinates[i];
          blend_vertices.push_back(vertex);
        }
        // A center vertex keeps the interpolation symmetric across the
        // quad instead of leaning on one diagonal
        SDL_Vertex center_vertex;
        center_vertex.position = tile_center;
        center_vertex.color = {brightness, brightness, brightness, alpha_sum / 4.0f};
        center_vertex.tex_coord = {0.5f, 0.5f};
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
  for (auto &batch : batches) {
    SDL_Texture * texture = this->terrain_textures.contains(batch.first) ? this->terrain_textures[batch.first] : nullptr;
    SDL_RenderGeometry(renderer, texture, batch.second.data(), (int) batch.second.size(),
                       batch_indices[batch.first].data(), (int) batch_indices[batch.first].size());
  }
  // Higher type numbers first so the lower, higher priority types land on
  // top where overlays stack
  for (auto batch = blend_batches.rbegin(); batch != blend_batches.rend(); ++batch) {
    SDL_RenderGeometry(renderer, this->terrain_textures[batch->first], batch->second.data(),
                       (int) batch->second.size(), blend_indices[batch->first].data(),
                       (int) blend_indices[batch->first].size());
  }

  this->drawObjects(renderer, window_rect, center_x, center_y);
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

// Art locations differ per category: plain objects have an idle animation
// under objects/<code>, fences have one animation per direction and paths
// have numbered shape pieces picked by which neighbors are also paths
Animation * MapView::objectAnimation(const ZooObject * object, std::string &draw_key) {
  std::string cache_key;
  std::string animation_path;
  if (object->category == "fences") {
    // Fence pieces sit on tile edges at half tile positions: 0 is the NE
    // facing piece of an x axis run, 6 the SE facing piece of a y axis
    // run. On a sloped edge the piece uses its 30 degree slope art:
    // idle30p rises along the edge's axis, idle30n falls.
    draw_key = rotationDirection(object->rotation);
    std::string state = "idle";
    float edge_start = 0.0f;
    float edge_end = 0.0f;
    this->fenceEdgeHeights(object, &edge_start, &edge_end);
    if (edge_end > edge_start) {
      state = "idle30p";
    } else if (edge_end < edge_start) {
      state = "idle30n";
    }
    animation_path = "fences/" + object->subcategory + "/" + object->code + "/" + state + "/" + state;
    cache_key = animation_path;
  } else if (object->category == "paths") {
    // Piece 5 is the isolated piece, the 16 neighbor combinations follow
    // it. The mask bits are in screen directions, so each neighbor's bit
    // depends on where the map orientation puts it on screen. The bit per
    // screen direction in the SE, SW, NW, NE ring:
    static const int screen_direction_bit[4] = {2, 4, 8, 1};
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
  for (const ZooObject * object : this->sorted_objects) {
    // Positions are in 64ths of a tile
    float tile_x = (float) object->x / 64.0f;
    float tile_y = (float) object->y / 64.0f;
    float world_x;
    float world_y;
    this->tileToWorld(tile_x, tile_y, &world_x, &world_y);
    // Anchor the sprite at the interpolated terrain height under it so
    // objects follow slopes. For a fence piece on a sloped edge this is
    // the edge midpoint, which is where its 30 degree art anchors
    // (verified pixel aligned against the original on fshore.zoo).
    world_y -= this->heightAt(tile_x, tile_y) * HEIGHT_STEP;
    float screen_x = (world_x - this->camera_x) * this->zoom + center_x;
    float screen_y = (world_y - this->camera_y) * this->zoom + center_y;
    if (screen_x < -200.0f || screen_x > window_rect->w + 200.0f ||
        screen_y < -200.0f || screen_y > window_rect->h + 200.0f) {
      continue;
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
    // Mirroring flips the box around the anchor point too
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
