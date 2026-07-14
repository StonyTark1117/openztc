#ifndef INPUT_HPP
#define INPUT_HPP

#include <SDL3/SDL.h>

enum class InputType {
  NONE,
  POSITIONED,
  BUTTON,
};

enum class InputEvent {
  NONE,
  LEFT_CLICK,
  LEFT_RELEASE,
  RIGHT_CLICK,
  CURSOR_MOVE,
  SCROLL,
  ZOOM_IN,
  ZOOM_OUT,
  BACK,
  QUIT
};

typedef struct {
  InputType type;
  InputEvent event;
  SDL_FPoint position;
  // Scroll direction for SCROLL events, positive is away from the user
  float scroll = 0.0f;
} Input;

#endif // INPUT_HPP