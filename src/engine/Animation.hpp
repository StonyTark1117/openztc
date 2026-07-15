#ifndef ANIMATION_HPP
#define ANIMATION_HPP

#include <string>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>

#include "IniReader.hpp"
#include "CompassDirection.hpp"
#include "PalletManager.hpp"
#include "Pallet.hpp"
#include "AnimationData.hpp"

class Animation {
public:
    Animation(std::unordered_map<std::string, AnimationData *> * data);
    ~Animation();

    void draw(SDL_Renderer * renderer, float x, float y, CompassDirection direction=CompassDirection::N);
    void draw(SDL_Renderer * renderer, SDL_FRect * draw_rect, CompassDirection direction=CompassDirection::N);
    bool getSize(float * w, float * h, CompassDirection direction=CompassDirection::N);
    bool getSizeByKey(const std::string &key, float * w, float * h);
    // The sprite box relative to the world anchor point, from the ani file
    void setBox(float x0, float y0, float x1, float y1);
    bool getBox(float * x0, float * y0, float * width, float * height);
    void drawByKey(SDL_Renderer * renderer, SDL_FRect * dest_rect, const std::string &key, bool mirrored=false);

    void queryTexture(CompassDirection direction, float * w, float * h);
private:
    int current_frame = 0;
    CompassDirection last_direction = CompassDirection::N;
    SDL_FlipMode renderer_flip = SDL_FLIP_NONE;
    uint32_t frame_start_time = 0;

    uint32_t frame_time_in_ms = 0;
    bool has_background = 0;
    bool has_box = false;
    float box_x0 = 0.0f;
    float box_y0 = 0.0f;
    float box_x1 = 0.0f;
    float box_y1 = 0.0f;

    std::unordered_map<std::string, std::vector<SDL_Surface *>> surfaces;
    std::unordered_map<std::string, std::vector<SDL_Texture *>> textures;

    template <typename T>
    std::string convertCompassDirectionToExistingAnimationString(CompassDirection direction, std::unordered_map<std::string, T> &animation_map);
    std::string convertCompassDirectionToString(CompassDirection direction);  // TODO: Figure out if this should be here

    void loadSurfaces(std::string direction_string, AnimationData * data);
};

#endif // ANIMATION_HPP