#include "InputManager.hpp"

InputManager::InputManager() {

}

InputManager::~InputManager() {

}

std::vector<Input> InputManager::getInputs() {
  std::vector<Input> inputs;
  SDL_Event event;

  while (SDL_PollEvent(&event)) {
    Input input = {
      .type = InputType::NONE,
      .event = InputEvent::NONE,
      .position = {0, 0},
    };

    switch (event.type) {
      case SDL_EVENT_QUIT:
        input.type = InputType::BUTTON;
        input.event = InputEvent::QUIT;
        break;
      case SDL_EVENT_KEY_DOWN:
        input.type = InputType::BUTTON;
        switch (event.key.key) {
          case SDLK_ESCAPE:
            input.event = InputEvent::BACK;
            break;
          case SDLK_PLUS:
          case SDLK_EQUALS:
          case SDLK_KP_PLUS:
            input.event = InputEvent::ZOOM_IN;
            break;
          case SDLK_MINUS:
          case SDLK_KP_MINUS:
            input.event = InputEvent::ZOOM_OUT;
            break;
          case SDLK_SPACE:
          case SDLK_P:
            input.event = InputEvent::PAUSE_TOGGLE;
            break;
          default:
            input.event = InputEvent::NONE;
            break;
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        input.type = InputType::POSITIONED;
        SDL_GetMouseState(&input.position.x, &input.position.y);
        input.event = getEventFromMouseButton(event.button.button);
        break;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT) {
          input.type = InputType::POSITIONED;
          SDL_GetMouseState(&input.position.x, &input.position.y);
          input.event = InputEvent::LEFT_RELEASE;
        }
        break;
      case SDL_EVENT_MOUSE_WHEEL:
        input.type = InputType::POSITIONED;
        SDL_GetMouseState(&input.position.x, &input.position.y);
        input.event = InputEvent::SCROLL;
        input.scroll = event.wheel.y;
        break;
      case SDL_EVENT_MOUSE_MOTION:
        input.type = InputType::POSITIONED;
        input.event = InputEvent::CURSOR_MOVE;
        SDL_GetMouseState(&input.position.x, &input.position.y);
        break;
    }
    if (input.type != InputType::NONE && input.event != InputEvent::NONE) {
        inputs.push_back(input);
    }
  }

  return inputs;
}

InputEvent InputManager::getEventFromMouseButton(Uint8 button) {
    InputEvent event;

    switch (button) {
        case SDL_BUTTON_LEFT:
            event = InputEvent::LEFT_CLICK;
            break;
        case SDL_BUTTON_RIGHT:
            event = InputEvent::RIGHT_CLICK;
            break;
        case SDL_BUTTON_MIDDLE:
            event = InputEvent::NONE;
            break;
        default:
            event = InputEvent::NONE;
            break;
    }

    return event;
}