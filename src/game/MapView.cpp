#include "MapView.hpp"

#include <algorithm>

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

// The corner height grid is the average of the heights of the tiles
// touching each corner, which gives the terrain its slopes. The per tile
// shape bitfield will refine this once its bit layout is confirmed.
void MapView::buildCornerHeights() {
  uint32_t width = this->zoo->getWidth();
  uint32_t height = this->zoo->getHeight();
  this->corner_heights.assign((size_t) (width + 1) * (height + 1), 0.0f);
  for (uint32_t corner_y = 0; corner_y <= height; corner_y++) {
    for (uint32_t corner_x = 0; corner_x <= width; corner_x++) {
      float total = 0.0f;
      int count = 0;
      for (int dy = -1; dy <= 0; dy++) {
        for (int dx = -1; dx <= 0; dx++) {
          int tile_x = (int) corner_x + dx;
          int tile_y = (int) corner_y + dy;
          if (tile_x < 0 || tile_y < 0 || tile_x >= (int) width || tile_y >= (int) height) {
            continue;
          }
          total += (float) this->zoo->getTile(tile_x, tile_y).height;
          count++;
        }
      }
      this->corner_heights[(size_t) corner_y * (width + 1) + corner_x] = count > 0 ? total / (float) count : 0.0f;
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
  std::unordered_map<int, std::vector<SDL_Vertex>> blend_batches;
  std::unordered_map<int, std::vector<int>> blend_indices;

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
        corner_h[i] = this->cornerHeight(cx, cy);
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

      // Slope shading: tiles facing away from the light are darker
      float slope = (corner_h[0] - corner_h[2]) * 0.15f;
      float brightness = 0.85f + slope;
      if (brightness < 0.4f) {
        brightness = 0.4f;
      }
      if (brightness > 1.0f) {
        brightness = 1.0f;
      }
      SDL_FColor color = {brightness, brightness, brightness, 1.0f};

      int type = this->terrain_textures.contains(tile.type) ? tile.type : 0;
      std::vector<SDL_Vertex> &vertices = batches[type];
      std::vector<int> &indices = batch_indices[type];
      int base = (int) vertices.size();
      const SDL_FPoint texture_coordinates[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
      for (int i = 0; i < 4; i++) {
        SDL_Vertex vertex;
        vertex.position = corners[i];
        vertex.color = color;
        vertex.tex_coord = texture_coordinates[i];
        vertices.push_back(vertex);
      }
      indices.push_back(base);
      indices.push_back(base + 1);
      indices.push_back(base + 2);
      indices.push_back(base);
      indices.push_back(base + 2);
      indices.push_back(base + 3);

      // Where a different terrain type borders this tile its texture
      // bleeds in, fading from the shared edge to the tile center. Lower
      // type numbers paint over higher ones so each boundary blends once.
      SDL_FPoint tile_center = {
        (corners[0].x + corners[1].x + corners[2].x + corners[3].x) / 4.0f,
        (corners[0].y + corners[1].y + corners[2].y + corners[3].y) / 4.0f,
      };
      const int edge_corners[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
      const int neighbor_offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
      for (int edge = 0; edge < 4; edge++) {
        int neighbor_x = (int) tile_x + neighbor_offsets[edge][0];
        int neighbor_y = (int) tile_y + neighbor_offsets[edge][1];
        if (neighbor_x < 0 || neighbor_y < 0 || neighbor_x >= (int) width || neighbor_y >= (int) height) {
          continue;
        }
        int neighbor_type = this->zoo->getTile(neighbor_x, neighbor_y).type;
        if (neighbor_type >= tile.type || !this->terrain_textures.contains(neighbor_type)) {
          continue;
        }
        std::vector<SDL_Vertex> &blend_vertices = blend_batches[neighbor_type];
        std::vector<int> &blend_index_list = blend_indices[neighbor_type];
        int blend_base = (int) blend_vertices.size();
        SDL_FColor edge_color = {brightness, brightness, brightness, 0.85f};
        SDL_FColor center_color = {brightness, brightness, brightness, 0.0f};
        for (int i = 0; i < 2; i++) {
          SDL_Vertex vertex;
          vertex.position = corners[edge_corners[edge][i]];
          vertex.color = edge_color;
          vertex.tex_coord = texture_coordinates[edge_corners[edge][i]];
          blend_vertices.push_back(vertex);
        }
        SDL_Vertex center_vertex;
        center_vertex.position = tile_center;
        center_vertex.color = center_color;
        center_vertex.tex_coord = {0.5f, 0.5f};
        blend_vertices.push_back(center_vertex);
        blend_index_list.push_back(blend_base);
        blend_index_list.push_back(blend_base + 1);
        blend_index_list.push_back(blend_base + 2);
      }
    }
  }

  for (auto &batch : batches) {
    SDL_Texture * texture = this->terrain_textures.contains(batch.first) ? this->terrain_textures[batch.first] : nullptr;
    SDL_RenderGeometry(renderer, texture, batch.second.data(), (int) batch.second.size(),
                       batch_indices[batch.first].data(), (int) batch_indices[batch.first].size());
  }
  for (auto &batch : blend_batches) {
    SDL_RenderGeometry(renderer, this->terrain_textures[batch.first], batch.second.data(), (int) batch.second.size(),
                       blend_indices[batch.first].data(), (int) blend_indices[batch.first].size());
  }

  this->drawObjects(renderer, window_rect, center_x, center_y);
}

// The rotation field steps in increments of 2 per quarter turn. The ring is
// in clockwise screen order, so rotating the map by a quarter turn shifts
// which direction art a facing needs by one step.
std::string MapView::rotationDirection(uint32_t rotation) {
  const char * directions[4] = {"SE", "SW", "NW", "NE"};
  return directions[((rotation / 2) + 4 - (uint32_t) this->orientation) % 4];
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

// Guests, animals and staff move around in the simulation, they are not
// static scenery and get skipped until the simulation drives them
static bool isEntityCategory(const std::string &category) {
  return category == "animals" || category == "guests" || category == "keeper" ||
         category == "maint" || category == "tour" || category == "helicopter";
}

// Art locations differ per category: plain objects have an idle animation
// under objects/<code>, fences have one animation per direction and paths
// have numbered shape pieces picked by which neighbors are also paths
Animation * MapView::objectAnimation(const ZooObject * object, std::string &draw_key) {
  std::string cache_key;
  std::string animation_path;
  if (object->category == "fences") {
    // Fence pieces sit on tile edges at half tile positions and their
    // rotation ring is one step behind the object one: 0 is the NE facing
    // piece of an x axis run, 6 the SE facing piece of a y axis run
    draw_key = rotationDirection(object->rotation + 6);
    animation_path = "fences/" + object->subcategory + "/" + object->code + "/idle/idle";
    cache_key = animation_path;
  } else if (object->category == "paths") {
    // Piece 5 is the isolated piece, the 16 neighbor combinations follow
    // it. The mask bits are in screen directions, so each neighbor's bit
    // depends on where the map orientation puts it on screen. The bit per
    // screen direction in the SE, SW, NW, NE ring:
    static const int screen_direction_bit[4] = {2, 4, 8, 1};
    uint32_t tile_x = object->x / 64;
    uint32_t tile_y = object->y / 64;
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
    animation_path = "paths/" + object->code + "/idle/idle";
    cache_key = animation_path;
  } else if (object->category == "ambient") {
    // Ambient markers are sound emitters without art
    return nullptr;
  } else if (isEntityCategory(object->category)) {
    return nullptr;
  } else {
    // Tank walls carry a piece code like fences do, plain objects face
    // where their rotation points
    if (object->category == "tankwall") {
      draw_key = rotationDirection(object->rotation + 6);
    } else {
      draw_key = rotationDirection(object->rotation);
    }
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
    // Anchor the sprite at the terrain height of its tile
    uint32_t corner_x = (uint32_t) tile_x;
    uint32_t corner_y = (uint32_t) tile_y;
    if (corner_x <= this->zoo->getWidth() && corner_y <= this->zoo->getHeight()) {
      world_y -= this->cornerHeight(corner_x, corner_y) * HEIGHT_STEP;
    }
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
    // Art without the wanted direction, like single state food pieces,
    // draws its default animation instead
    if (!draw_key.empty()) {
      float key_width = 0.0f;
      float key_height = 0.0f;
      if (!animation->getSizeByKey(draw_key, &key_width, &key_height)) {
        draw_key.clear();
      }
    }
    // Anchor the sprite by the box its ani file declares around the world
    // anchor point, falling back to bottom center for art without one.
    // Fence pieces still use the fallback until per frame offsets are
    // implemented, their boxes leave gaps between the pieces.
    float box_x0 = 0.0f;
    float box_y0 = 0.0f;
    float sprite_width = 0.0f;
    float sprite_height = 0.0f;
    if (object->category == "fences" ||
        !animation->getBox(&box_x0, &box_y0, &sprite_width, &sprite_height)) {
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
    SDL_FRect destination = {
      screen_x + box_x0 * this->zoom,
      screen_y + box_y0 * this->zoom,
      sprite_width * this->zoom,
      sprite_height * this->zoom,
    };
    if (!draw_key.empty()) {
      animation->drawByKey(renderer, &destination, draw_key);
    } else {
      animation->draw(renderer, &destination);
    }
  }
}
