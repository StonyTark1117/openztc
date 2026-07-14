#include "MapView.hpp"

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
  SDL_Log("Loaded map %s: %ux%u tiles", zoo_file_name.c_str(), this->zoo->getWidth(), this->zoo->getHeight());
  this->buildCornerHeights();
  // Start looking at the middle of the map
  this->camera_x = 0.0f;
  this->camera_y = (float) (this->zoo->getWidth() + this->zoo->getHeight()) / 2.0f * TILE_HALF_HEIGHT;
  return true;
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
  // independent of the map size
  std::unordered_map<int, std::vector<SDL_Vertex>> batches;
  std::unordered_map<int, std::vector<int>> batch_indices;

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
        float world_x = ((float) cx - (float) cy) * TILE_HALF_WIDTH;
        float world_y = ((float) cx + (float) cy) * TILE_HALF_HEIGHT - corner_h[i] * HEIGHT_STEP;
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
    }
  }

  for (auto &batch : batches) {
    SDL_Texture * texture = this->terrain_textures.contains(batch.first) ? this->terrain_textures[batch.first] : nullptr;
    SDL_RenderGeometry(renderer, texture, batch.second.data(), (int) batch.second.size(),
                       batch_indices[batch.first].data(), (int) batch_indices[batch.first].size());
  }
}
