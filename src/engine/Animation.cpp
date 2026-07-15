#include "Animation.hpp"

#include <assert.h>

Animation::Animation(std::unordered_map<std::string, AnimationData *> * data) {
  for(auto map_entry : *data) {
    this->loadSurfaces(map_entry.first, map_entry.second);
  }
}

Animation::~Animation() {
  for (auto surface_list : this->surfaces) {
    for (SDL_Surface * surface : surface_list.second) {
      SDL_DestroySurface(surface);
    }
    surface_list.second.clear();
  }
  for (auto texture_list : this->textures) {
    for (SDL_Texture * texture : texture_list.second) {
      SDL_DestroyTexture(texture);
    }
    texture_list.second.clear();
  }
}

void Animation::draw(SDL_Renderer *renderer,  float x, float y, CompassDirection direction) {
  std::string direction_string = convertCompassDirectionToExistingAnimationString(direction, this->textures);
  SDL_FRect rect = {x, y, 0.0f, 0.0f};
  if (!this->textures[direction_string].empty()) {
    SDL_GetTextureSize(this->textures[direction_string][this->current_frame], &rect.w, &rect.h);
  } else {
    direction_string = convertCompassDirectionToExistingAnimationString(direction, this->surfaces);
    rect.w = this->surfaces[direction_string][this->current_frame]->w;
    rect.h = this->surfaces[direction_string][this->current_frame]->h;
  }
  assert(rect.h > 0);
  assert(rect.w > 0);
  this->draw(renderer, &rect, direction);
}

bool Animation::getSize(float * w, float * h, CompassDirection direction) {
  std::string direction_string = convertCompassDirectionToExistingAnimationString(direction, this->textures);
  if (!direction_string.empty() && !this->textures[direction_string].empty()) {
    return SDL_GetTextureSize(this->textures[direction_string][0], w, h);
  }
  direction_string = convertCompassDirectionToExistingAnimationString(direction, this->surfaces);
  if (!direction_string.empty() && !this->surfaces[direction_string].empty()) {
    *w = (float) this->surfaces[direction_string][0]->w;
    *h = (float) this->surfaces[direction_string][0]->h;
    return true;
  }
  return false;
}

void Animation::setBox(float x0, float y0, float x1, float y1) {
  this->box_x0 = x0;
  this->box_y0 = y0;
  this->box_x1 = x1;
  this->box_y1 = y1;
  this->has_box = x1 > x0 && y1 > y0;
}

bool Animation::getBox(float * x0, float * y0, float * width, float * height) {
  if (!this->has_box) {
    return false;
  }
  *x0 = this->box_x0;
  *y0 = this->box_y0;
  *width = this->box_x1 - this->box_x0;
  *height = this->box_y1 - this->box_y0;
  return true;
}

bool Animation::getSizeByKey(const std::string &key, float * w, float * h) {
  if (this->textures.contains(key) && !this->textures[key].empty()) {
    return SDL_GetTextureSize(this->textures[key][0], w, h);
  }
  if (this->surfaces.contains(key) && !this->surfaces[key].empty()) {
    *w = (float) this->surfaces[key][0]->w;
    *h = (float) this->surfaces[key][0]->h;
    return true;
  }
  return false;
}

void Animation::drawByKey(SDL_Renderer * renderer, SDL_FRect * dest_rect, const std::string &key, bool mirrored) {
  if (!this->textures.contains(key) || this->textures[key].empty()) {
    if (!this->surfaces.contains(key) || this->surfaces[key].empty()) {
      return;
    }
    this->textures[key] = std::vector<SDL_Texture *>();
    for (SDL_Surface * surface : this->surfaces[key]) {
      SDL_Texture * texture = SDL_CreateTextureFromSurface(renderer, surface);
      // Point sampling like the original's blitter; linear would bleed the
      // transparent edge into each sprite, seaming tiled pieces like fences
      SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
      this->textures[key].push_back(texture);
      SDL_DestroySurface(surface);
    }
    this->surfaces[key].clear();
  }
  if (this->textures[key].empty() || this->textures[key][0] == nullptr) {
    return;
  }
  // Cycle the frames by time so map sprites animate. The background frame
  // of FATZ animations sits at the end and is not part of the cycle.
  size_t frame_count = this->textures[key].size() - (size_t) (this->has_background ? 1 : 0);
  size_t frame = 0;
  if (this->frame_time_in_ms > 0 && frame_count > 1) {
    frame = (SDL_GetTicks() / this->frame_time_in_ms) % frame_count;
  }
  if (this->textures[key][frame] == nullptr) {
    frame = 0;
  }
  if (mirrored) {
    // West side views of most entity art are the east side ones flipped
    SDL_RenderTextureRotated(renderer, this->textures[key][frame], NULL, dest_rect, 0.0, NULL,
                             SDL_FLIP_HORIZONTAL);
  } else {
    SDL_RenderTexture(renderer, this->textures[key][frame], NULL, dest_rect);
  }
}

void Animation::draw(SDL_Renderer *renderer,  SDL_FRect * dest_rect, CompassDirection direction) {
  // Resolve against the already converted textures first. Resolving against
  // the surfaces when a converted direction exists would overwrite its
  // textures with an empty list, since the surfaces are consumed on
  // conversion.
  std::string direction_string = convertCompassDirectionToExistingAnimationString(direction, this->textures);
  if (direction_string.empty() || this->textures[direction_string].empty()) {
    direction_string = convertCompassDirectionToExistingAnimationString(direction, this->surfaces);
    if (direction_string.empty()) {
      SDL_Log("Cannot draw animation because the specified direction does not exist");
      return;
    }
    this->textures[direction_string] = std::vector<SDL_Texture *>();
    for (SDL_Surface * surface: this->surfaces[direction_string]) {
      SDL_Texture * texture = SDL_CreateTextureFromSurface(renderer, surface);
      SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
      this->textures[direction_string].push_back(texture);
      SDL_DestroySurface(surface);
    }
    this->surfaces[direction_string].clear();
  }

  if (this->textures[direction_string].empty()) {
    return;
  }

  if (direction != this->last_direction) {
    this->last_direction = direction;
    this->current_frame = 0;
    this->frame_start_time = SDL_GetTicks();
  } else {
    if (this->frame_time_in_ms < SDL_GetTicks() - this->frame_start_time) {
      this->current_frame++;
      this->frame_start_time = SDL_GetTicks();
    }
  }

  if (this->current_frame >= (int) this->textures[direction_string].size()) {
    this->current_frame = 0;
  }

  #ifdef DEBUG
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 100);
    SDL_RenderFillRect(renderer, dest_rect);
  #endif

  if (dest_rect->w == 0 || dest_rect->h == 0) {
    SDL_GetTextureSize(this->textures[direction_string][this->current_frame], &dest_rect->w, &dest_rect->h);
  }

  // Draw background
  if (this->has_background) {
    SDL_RenderTextureRotated(renderer, this->textures[direction_string][this->textures[direction_string].size() - 1], NULL, dest_rect, 0, NULL, this->renderer_flip);
    if (this->current_frame >= (int) this->textures[direction_string].size() - 1) {
      this->current_frame = 0;
    }
  }

  // Draw object
  SDL_RenderTextureRotated(renderer, this->textures[direction_string][this->current_frame], NULL, dest_rect, 0, NULL, this->renderer_flip);
}

void Animation::queryTexture(CompassDirection direction, float * w, float * h) {
  std::string direction_string = convertCompassDirectionToExistingAnimationString(direction, this->textures);
  if (!this->textures[direction_string].empty()) {
    SDL_GetTextureSize(this->textures[direction_string][this->current_frame], w, h);
  } else {
    direction_string = convertCompassDirectionToExistingAnimationString(direction, this->surfaces);
    if (w != nullptr) {
      *w = this->surfaces[direction_string][this->current_frame]->w;
    }
    if (h != nullptr) {
      *h = this->surfaces[direction_string][this->current_frame]->h;
    }
  }
}

std::string Animation::convertCompassDirectionToString(CompassDirection direction) {
  std::string direction_string = "";
  
  switch (direction) {
    case CompassDirection::N:
      direction_string = "N";
      break;
    case CompassDirection::NE:
        direction_string = "NE";
      break;
    case CompassDirection::NW:
      direction_string = "NW";
      break;
    case CompassDirection::S:
      direction_string = "S";
      break;
    case CompassDirection::SE:
        direction_string = "SE";
      break;
    case CompassDirection::SW:
        direction_string = "SW";
      break;
    case CompassDirection::E:
        direction_string = "E";
      break;
    case CompassDirection::W:
        direction_string = "W";
        break;
    case CompassDirection::G:
      direction_string = "G";
      break;
    case CompassDirection::H:
      direction_string = "H";
      break;
    default:
      SDL_Log("Direction used has no string equivalent direction used!");
      break;
  }

  return direction_string;
}

template <typename T>
std::string Animation::convertCompassDirectionToExistingAnimationString(CompassDirection direction, std::unordered_map<std::string, T> &animation_map) {
  std::string direction_string = "";
  
  switch (direction) {
    case CompassDirection::N:
      if (animation_map.contains("N")) {
        direction_string = "N";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("NE")) {
        direction_string = "NE";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("NW")) {
        direction_string = "NW";
        this->renderer_flip = SDL_FLIP_NONE;
      }
      break;
    case CompassDirection::NE:
      if (animation_map.contains("NE")) {
        direction_string = "NE";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("NW")) {
        direction_string = "NW";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("N")) {
        direction_string = "N";
        this->renderer_flip = SDL_FLIP_NONE;
      }
      break;
    case CompassDirection::NW:
      if (animation_map.contains("NW")) {
        direction_string = "NW";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("NE")) {
        direction_string = "NE";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("N")) {
        direction_string = "N";
        this->renderer_flip = SDL_FLIP_NONE;
      }
      break;
    case CompassDirection::S:
      if (animation_map.contains("S")) {
        direction_string = "S";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("SE")) {
        direction_string = "SE";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("SW")) {
        direction_string = "SW";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("N")) {
        direction_string = "N";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("NE")) {
        direction_string = "NE";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("NW")) {
        direction_string = "NE";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("E")) {
        direction_string = "E";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("W")) {
        direction_string = "W";
        this->renderer_flip = SDL_FLIP_NONE;
      }
      break;
    case CompassDirection::SE:
      if (animation_map.contains("SE")) {
        direction_string = "SE";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("SW")) {
        direction_string = "SW";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("E")) {
        direction_string = "E";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("W")) {
        direction_string = "W";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("S")) {
        direction_string = "S";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("N")) {
        direction_string = "N";
        this->renderer_flip = SDL_FLIP_NONE;
      }
      break;
    case CompassDirection::SW:
      if (animation_map.contains("SW")) {
        direction_string = "SW";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("SE")) {
        direction_string = "SE";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("S")) {
        direction_string = "S";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("W")) {
        direction_string = "W";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("E")) {
        direction_string = "E";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("NW")) {
        direction_string = "SW";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("NE")) {
        direction_string = "NE";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("N")) {
        direction_string = "N";
        this->renderer_flip = SDL_FLIP_NONE;
      }
      break;
    case CompassDirection::E:
      if (animation_map.contains("E")) {
        direction_string = "E";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("W")) {
        direction_string = "W";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("NE")) {
        direction_string = "NE";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("SE")) {
        direction_string = "SE";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("NW")) {
        direction_string = "NW";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("SW")) {
        direction_string = "SW";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("N")) {
        direction_string = "N";
        this->renderer_flip = SDL_FLIP_NONE;
      }
      break;
    case CompassDirection::W:
      if (animation_map.contains("W")) {
        direction_string = "W";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("E")) {
        direction_string = "E";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("NW")) {
        direction_string = "NW";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("SW")) {
        direction_string = "SW";
        this->renderer_flip = SDL_FLIP_NONE;
      } else if (animation_map.contains("NE")) {
        direction_string = "NE";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("SE")) {
        direction_string = "SE";
        this->renderer_flip = SDL_FLIP_HORIZONTAL;
      } else if (animation_map.contains("N")) {
        direction_string = "N";
        this->renderer_flip = SDL_FLIP_NONE;
      }
      break;
    case CompassDirection::G:
        if (animation_map.contains("G")) {
          direction_string = "G";
          this->renderer_flip = SDL_FLIP_NONE;
        } else {
          direction_string = "N";
          this->renderer_flip = SDL_FLIP_NONE;
        }
        break;
    case CompassDirection::H:
        if (animation_map.contains("H")) {
          direction_string = "H";
          this->renderer_flip = SDL_FLIP_NONE;
        } else {
          direction_string = "N";
          this->renderer_flip = SDL_FLIP_NONE;
        }
        break;
    default:
      SDL_Log("Unhandled direction used!");
      break;
  }

  return direction_string;
}



void Animation::loadSurfaces(std::string direction_string, AnimationData * data) {
  if (data == nullptr || data->frame_count == 0) {
    SDL_Log("No frames in animation data");
    return;
  }
  if (data->pallet == nullptr) {
    SDL_Log("Animation %s has no pallet, skipping", direction_string.c_str());
    return;
  }
  this->frame_time_in_ms = data->frame_time_in_ms;
  this->has_background = data->has_background;

  assert(data != nullptr);
  assert(data->width > 0);
  assert(data->height > 0);

  // Frames sit relative to the anchor point of the sprite box
  int16_t offset_x = data->origin_x;
  int16_t offset_y = data->origin_y;

  this->surfaces[direction_string] = std::vector<SDL_Surface *>();
  for(int i = 0; i < ((int) data->frame_count + (int) data->has_background); i++) {
    this->surfaces[direction_string].push_back(SDL_CreateSurface(data->width, data->height, SDL_PIXELFORMAT_RGBA32));
    for(int y = 0; y < data->frames[i].height; y++) {
      int x = offset_x - data->frames[i].offset_x;
      int row = y + offset_y - data->frames[i].offset_y;
      for(int instruction = 0; instruction < data->frames[i].lines[y].instruction_count; instruction++) {
        x += data->frames[i].lines[y].instructions[instruction].offset;
        for(int p = 0; p < data->frames[i].lines[y].instructions[instruction].color_count; p++, x++) {
          if (x < 0 || x >= data->width || row < 0 || row >= data->height) {
              SDL_Log("Failure to draw within the bounds. %i,%i does not fit in a %i,%i rectangle", x, row, data->width, data->height);
              break;
            }
            if (data->frames[i].is_shadow) {
              ((uint32_t *) this->surfaces[direction_string][i]->pixels)[data->width * row + x] = 0xFF000000;
            } else {
              ((uint32_t *) this->surfaces[direction_string][i]->pixels)[data->width * row + x] = data->pallet->colors[data->frames[i].lines[y].instructions[instruction].colors[p]];
            }
        }
      }
    }
  }
  assert(!this->surfaces[direction_string].empty());
  free(data);
}
