#include "ZtMiniMap.hpp"

ZtMiniMap::ZtMiniMap(IniReader * ini_reader, ResourceManager * resource_manager, std::string name) {
  this->ini_reader = ini_reader;
  this->resource_manager = resource_manager;
  this->name = name;

  this->id = ini_reader->getInt(name, "id");
  this->layer = ini_reader->getInt(name, "layer", 1);
  if (ini_reader->getInt(name, "anchor", 0) != 0) {
    this->anchors.push_back(ini_reader->getInt(name, "anchor"));
  }
}

ZtMiniMap::~ZtMiniMap() {
  if (this->tile_surface != nullptr) {
    SDL_DestroySurface(this->tile_surface);
  }
  if (this->tile_texture != nullptr) {
    SDL_DestroyTexture(this->tile_texture);
  }
}

void ZtMiniMap::setTiles(const std::vector<uint8_t> &tile_colors, int width, int height) {
  if (this->tile_surface != nullptr) {
    SDL_DestroySurface(this->tile_surface);
    this->tile_surface = nullptr;
  }
  if (this->tile_texture != nullptr) {
    SDL_DestroyTexture(this->tile_texture);
    this->tile_texture = nullptr;
  }
  if (width <= 0 || height <= 0 || tile_colors.size() < (size_t) width * height * 4) {
    this->map_width = 0;
    this->map_height = 0;
    return;
  }
  this->map_width = width;
  this->map_height = height;
  this->tile_surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
  if (this->tile_surface != nullptr) {
    SDL_memcpy(this->tile_surface->pixels, tile_colors.data(), (size_t) width * height * 4);
  }
}

void ZtMiniMap::setTileTransform(SDL_FPoint origin, SDL_FPoint x_axis_end, SDL_FPoint y_axis_end) {
  this->unit_origin = origin;
  this->unit_x_end = x_axis_end;
  this->unit_y_end = y_axis_end;
}

void ZtMiniMap::setViewQuad(const SDL_FPoint quad[4]) {
  for (int i = 0; i < 4; i++) {
    this->view_quad[i] = quad[i];
  }
  this->has_view_quad = true;
}

SDL_FPoint ZtMiniMap::tileToWidget(float tile_x, float tile_y) {
  float fraction_x = this->map_width > 0 ? tile_x / (float) this->map_width : 0.0f;
  float fraction_y = this->map_height > 0 ? tile_y / (float) this->map_height : 0.0f;
  float unit_x = this->unit_origin.x + (this->unit_x_end.x - this->unit_origin.x) * fraction_x +
                 (this->unit_y_end.x - this->unit_origin.x) * fraction_y;
  float unit_y = this->unit_origin.y + (this->unit_x_end.y - this->unit_origin.y) * fraction_x +
                 (this->unit_y_end.y - this->unit_origin.y) * fraction_y;
  return {this->draw_rect.x + unit_x * this->draw_rect.w, this->draw_rect.y + unit_y * this->draw_rect.h};
}

UiAction ZtMiniMap::handleInputs(std::vector<Input> &inputs) {
  UiAction result = {Action::NONE, 0, 0};
  for (Input input : inputs) {
    if (input.type != InputType::POSITIONED || input.event != InputEvent::LEFT_CLICK) {
      continue;
    }
    if (input.position.x < this->draw_rect.x || input.position.x > this->draw_rect.x + this->draw_rect.w ||
        input.position.y < this->draw_rect.y || input.position.y > this->draw_rect.y + this->draw_rect.h ||
        this->map_width == 0) {
      continue;
    }
    // Solve the affine tile to unit transform backwards for the click
    float unit_x = (input.position.x - this->draw_rect.x) / this->draw_rect.w - this->unit_origin.x;
    float unit_y = (input.position.y - this->draw_rect.y) / this->draw_rect.h - this->unit_origin.y;
    float ax = this->unit_x_end.x - this->unit_origin.x;
    float ay = this->unit_x_end.y - this->unit_origin.y;
    float bx = this->unit_y_end.x - this->unit_origin.x;
    float by = this->unit_y_end.y - this->unit_origin.y;
    float determinant = ax * by - ay * bx;
    if (determinant == 0.0f) {
      continue;
    }
    float fraction_x = (unit_x * by - unit_y * bx) / determinant;
    float fraction_y = (ax * unit_y - ay * unit_x) / determinant;
    if (fraction_x < 0.0f || fraction_x > 1.0f || fraction_y < 0.0f || fraction_y > 1.0f) {
      // Inside the widget box but outside the map diamond
      continue;
    }
    this->clicked_tile_x = fraction_x * (float) this->map_width;
    this->clicked_tile_y = fraction_y * (float) this->map_height;
    result.source = this->id;
  }
  return result;
}

bool ZtMiniMap::getClickedTile(float * tile_x, float * tile_y) {
  if (this->map_width == 0) {
    return false;
  }
  *tile_x = this->clicked_tile_x;
  *tile_y = this->clicked_tile_y;
  return true;
}

void ZtMiniMap::draw(SDL_Renderer * renderer, SDL_FRect * layout_rect) {
  this->generateDrawRect(this->ini_reader->getSection(this->name), layout_rect);
  if (this->tile_surface != nullptr && this->tile_texture == nullptr) {
    this->tile_texture = SDL_CreateTextureFromSurface(renderer, this->tile_surface);
    SDL_SetTextureScaleMode(this->tile_texture, SDL_SCALEMODE_NEAREST);
  }
  if (this->tile_texture == nullptr) {
    return;
  }

  // The tile grid drawn as the diamond the transform describes
  SDL_FPoint corners[4] = {
    this->tileToWidget(0.0f, 0.0f),
    this->tileToWidget((float) this->map_width, 0.0f),
    this->tileToWidget((float) this->map_width, (float) this->map_height),
    this->tileToWidget(0.0f, (float) this->map_height),
  };
  const SDL_FPoint texture_coordinates[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
  SDL_Vertex vertices[4];
  for (int i = 0; i < 4; i++) {
    vertices[i].position = corners[i];
    vertices[i].color = {1.0f, 1.0f, 1.0f, 1.0f};
    vertices[i].tex_coord = texture_coordinates[i];
  }
  const int indices[6] = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(renderer, this->tile_texture, vertices, 4, indices, 6);

  if (this->has_view_quad) {
    SDL_FPoint outline[5];
    for (int i = 0; i < 4; i++) {
      SDL_FPoint point = this->tileToWidget(this->view_quad[i].x, this->view_quad[i].y);
      // Keep the outline inside the widget box
      point.x = SDL_clamp(point.x, this->draw_rect.x, this->draw_rect.x + this->draw_rect.w);
      point.y = SDL_clamp(point.y, this->draw_rect.y, this->draw_rect.y + this->draw_rect.h);
      outline[i] = point;
    }
    outline[4] = outline[0];
    SDL_SetRenderDrawColor(renderer, 255, 255, 191, 255);
    SDL_RenderLines(renderer, outline, 5);
  }
}
