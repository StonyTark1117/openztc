#ifndef MAP_VIEW_HPP
#define MAP_VIEW_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL.h>

#include "../engine/Animation.hpp"
#include "../engine/ResourceManager.hpp"
#include "../engine/ZooFile.hpp"
#include "../engine/Input.hpp"
#include "Simulation.hpp"

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
  void lookAtTile(float tile_x, float tile_y);
  // Map orientation in quarter turns, like the rotate camera option in game
  void setOrientation(int orientation);

  uint32_t getMapWidth() { return this->zoo != nullptr ? this->zoo->getWidth() : 0; }
  uint32_t getMapHeight() { return this->zoo != nullptr ? this->zoo->getHeight() : 0; }
  const ZooFile * getZoo() { return this->zoo; }
  // The path tiles as sorted keys for the simulation's world
  std::vector<uint64_t> getPathTileKeys();
  // The simulation's guests and animals, drawn on top of the map. While
  // the simulation drives the animals their static records stay hidden.
  void setSimGuests(const std::vector<SimGuest> &guests) { this->sim_guests = guests; }
  void setSimAnimals(const std::vector<SimAnimal> &animals) { this->sim_animals = animals; }
  // One RGBA color per tile for the minimap, terrain colored by the
  // miniclr sections with paths, foliage, fences and buildings over it
  std::vector<uint8_t> buildMinimapColors(IniReader * minimap_colors);
  // Position of a tile in the unit square around the rotated map, matching
  // what the minimap draws
  void tileToUnit(float tile_x, float tile_y, float * unit_x, float * unit_y);
  // The tile under a window position, ignoring terrain height
  void screenToTile(float screen_x, float screen_y, SDL_FRect * window_rect, float * tile_x, float * tile_y);

  // Returns false when the user wants to leave the map
  bool handleInputs(std::vector<Input> &inputs);
  void draw(SDL_Renderer * renderer, SDL_FRect * window_rect);

private:
  ResourceManager * resource_manager = nullptr;
  ZooFile * zoo = nullptr;

  std::unordered_map<int, SDL_Texture *> terrain_textures;
  // Terrain types with a water= flag in the tiletex configs
  std::unordered_set<int> water_terrain_types;
  bool textures_loaded = false;

  // Objects sorted in paint order and their animations by object code
  std::vector<const ZooObject *> sorted_objects;
  std::unordered_map<std::string, Animation *> object_animations;
  // The cfg files form a registry mapping object records to their ai
  // definition files, which name the art locations
  std::vector<IniReader *> registry_configs;
  bool registry_loaded = false;
  // Tiles carrying a path, for neighbor based path piece selection
  std::unordered_map<uint64_t, bool> path_tiles;
  std::vector<SimGuest> sim_guests;
  std::vector<SimAnimal> sim_animals;
  int missing_object_art = 0;

  // Camera offset in world pixels at zoom 1 and the zoom factor
  float camera_x = 0.0f;
  float camera_y = 0.0f;
  float zoom = 1.0f;
  // Orientation 1 matches the original game's default camera
  int orientation = 1;
  bool dragging = false;
  SDL_FPoint last_cursor = {0.0f, 0.0f};

  // Corner height grid, (width + 1) * (height + 1) entries
  std::vector<float> corner_heights;
  // Four entries per tile in NW, NE, SE, SW order. Adjacent tiles can
  // disagree about a shared corner where the terrain has a cliff.
  std::vector<float> tile_corner_heights;

  void loadTerrainTextures(SDL_Renderer * renderer);
  void drawObjects(SDL_Renderer * renderer, SDL_FRect * window_rect, float center_x, float center_y);
  Animation * objectAnimation(const ZooObject * object, std::string &draw_key);
  void buildCornerHeights();
  float cornerHeight(uint32_t x, uint32_t y);
  float tileCornerHeight(uint32_t x, uint32_t y, int corner);
  float heightAt(float tile_x, float tile_y);
  void fenceEdgeHeights(const ZooObject * object, float * start, float * end);
  void tileToWorld(float tile_x, float tile_y, float * world_x, float * world_y);
  std::string rotationDirection(uint32_t rotation);
  void sortObjects();
  void drawSimGuests(SDL_Renderer * renderer, float center_x, float center_y);
  void drawSimEntity(SDL_Renderer * renderer, float center_x, float center_y,
                     const std::string &animation_path, int32_t x, int32_t y, uint8_t facing);
  bool isWaterAt(int32_t x, int32_t y);
  Animation * firstAnimation(const std::vector<std::string> &candidates, const std::string &cache_key);
  void loadObjectRegistry();
  std::string registryLookup(const std::string &section, const std::string &key);
  std::string objectArtPath(const ZooObject * object);
};

#endif // MAP_VIEW_HPP
