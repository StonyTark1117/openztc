#ifndef MAP_VIEW_HPP
#define MAP_VIEW_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "../engine/Animation.hpp"
#include "../engine/ResourceManager.hpp"
#include "../engine/ZooFile.hpp"
#include "../engine/Input.hpp"

// Renders the terrain of a zoo map isometrically. The terrain textures and
// their type numbers come from the terrain/tiletex*.cfg files in the game
// data. The camera pans by dragging with the left mouse button and zooms
// with the scroll wheel.
class MapView {
public:
  MapView(ResourceManager * resource_manager);
  ~MapView();

  bool loadMap(const std::string &zoo_file_name);
  void setZoom(float zoom) { this->zoom = zoom; }

  // Returns false when the user wants to leave the map
  bool handleInputs(std::vector<Input> &inputs);
  void draw(SDL_Renderer * renderer, SDL_FRect * window_rect);

private:
  ResourceManager * resource_manager = nullptr;
  ZooFile * zoo = nullptr;

  std::unordered_map<int, SDL_Texture *> terrain_textures;
  bool textures_loaded = false;

  // Objects sorted in paint order and their animations by object code
  std::vector<const ZooObject *> sorted_objects;
  std::unordered_map<std::string, Animation *> object_animations;
  int missing_object_art = 0;

  // Camera offset in world pixels at zoom 1 and the zoom factor
  float camera_x = 0.0f;
  float camera_y = 0.0f;
  float zoom = 1.0f;
  bool dragging = false;
  SDL_FPoint last_cursor = {0.0f, 0.0f};

  // Corner height grid, (width + 1) * (height + 1) entries
  std::vector<float> corner_heights;

  void loadTerrainTextures(SDL_Renderer * renderer);
  void drawObjects(SDL_Renderer * renderer, SDL_FRect * window_rect, float center_x, float center_y);
  Animation * objectAnimation(const std::string &code);
  void buildCornerHeights();
  float cornerHeight(uint32_t x, uint32_t y);
};

#endif // MAP_VIEW_HPP
