#ifndef ZT_MINI_MAP_HPP
#define ZT_MINI_MAP_HPP

#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "UiElement.hpp"
#include "../engine/IniReader.hpp"
#include "../engine/ResourceManager.hpp"
#include "../engine/Input.hpp"

// The isometric overview map in the game toolbar. The game pushes a color
// per tile and an affine tile-to-diamond transform, the widget draws the
// diamond, the current view outline and reports clicks as tile positions.
class ZtMiniMap : public UiElement {
public:
  ZtMiniMap(IniReader * ini_reader, ResourceManager * resource_manager, std::string name);
  ~ZtMiniMap();

  UiAction handleInputs(std::vector<Input> &inputs);
  void draw(SDL_Renderer * renderer, SDL_FRect * layout_rect);

  // One RGBA color per tile, row by row
  void setTiles(const std::vector<uint8_t> &tile_colors, int width, int height);
  // Positions of the tile corners (0,0), (width,0) and (0,height) in the
  // unit square the widget draws, defining the map orientation
  void setTileTransform(SDL_FPoint origin, SDL_FPoint x_axis_end, SDL_FPoint y_axis_end);
  // The visible tile area, drawn as an outline
  void setViewQuad(const SDL_FPoint quad[4]);

  bool getClickedTile(float * tile_x, float * tile_y);

private:
  int map_width = 0;
  int map_height = 0;
  SDL_Surface * tile_surface = nullptr;
  SDL_Texture * tile_texture = nullptr;
  SDL_FPoint unit_origin = {0.0f, 0.0f};
  SDL_FPoint unit_x_end = {1.0f, 0.0f};
  SDL_FPoint unit_y_end = {0.0f, 1.0f};
  SDL_FPoint view_quad[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
  bool has_view_quad = false;
  float clicked_tile_x = 0.0f;
  float clicked_tile_y = 0.0f;

  SDL_FPoint tileToWidget(float tile_x, float tile_y);
};

#endif // ZT_MINI_MAP_HPP
