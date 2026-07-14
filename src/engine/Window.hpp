#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <SDL3/SDL.h>
#include <string>

class Window {
public:
    SDL_Window *window;
    SDL_Renderer *renderer;

    Window(const std::string &title, int width, int height, float fps_target);
    ~Window();

    unsigned int start_frame;
    float frame_delay;

    SDL_FRect * getWindowRect();

    void clear();
    void present();
private:
    SDL_FRect window_rect = {0.0f, 0.0f, 0.0f, 0.0f};
};

#endif // WINDOW_HPP