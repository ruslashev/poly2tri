#include "screen.hh"
#include "utils.hh"

screen::screen(int n_window_width, int n_window_height)
  : window_width(n_window_width), window_height(n_window_height) {
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

  _window = SDL_CreateWindow("poly2tri", SDL_WINDOWPOS_CENTERED
      , SDL_WINDOWPOS_CENTERED, window_width, window_height, SDL_WINDOW_OPENGL);

  _gl_context = SDL_GL_CreateContext(_window);

  GLenum err = glewInit();
  assertf(err == GLEW_OK, "failed to initialze glew: %s",
      glewGetErrorString(err));

  assertf(GLEW_VERSION_2_0, "your graphic card does not support OpenGL 2.0");

  running = true;
}

screen::~screen() {
  SDL_GL_DeleteContext(_gl_context);
  SDL_Quit();
}

static inline char sdlkey_to_char(const SDL_Keycode &kc) {
  switch (kc) {
    case SDLK_w: return 'w';
    case SDLK_a: return 'a';
    case SDLK_s: return 's';
    case SDLK_d: return 'd';
    default:     return -1;
  }
}

void screen::mainloop(void (*load_cb)(screen*)
    , void (*key_event_cb)(char, bool)
    , void (*mousemotion_event_cb)(float, float, int, int)
    , void (*mousebutton_event_cb)(int, bool)
    , void (*update_cb)(double, double, screen*)
    , void (*draw_cb)(double)
    , void (*cleanup_cb)(void)) {
  load_cb(this);

  const int ticks_per_second = 60, max_update_ticks = 5;
  // everywhere all time is measured in seconds unless otherwise stated
  double t = 0, dt = 1. / ticks_per_second;
  double current_time = get_time_in_seconds(), accumulator = 0;

  uint64_t total_frames = 0;
  int draw_count = 0;

  while (running) {
    double real_time = get_time_in_seconds()
      , elapsed = real_time - current_time;
    elapsed = std::min(elapsed, max_update_ticks * dt);
    current_time = real_time;
    accumulator += elapsed;

    while (accumulator >= dt) {
      { // events
        SDL_Event sdl_event;
        while (SDL_PollEvent(&sdl_event) != 0)
          if (sdl_event.type == SDL_QUIT)
            running = false;
          else if ((sdl_event.type == SDL_KEYDOWN || sdl_event.type == SDL_KEYUP)
              && sdl_event.key.repeat == 0) {
            const char key_info = sdlkey_to_char(sdl_event.key.keysym.sym);
            if (key_info != -1)
              key_event_cb(key_info, sdl_event.type == SDL_KEYDOWN);
          } else if (sdl_event.type == SDL_MOUSEMOTION)
            mousemotion_event_cb(sdl_event.motion.xrel, sdl_event.motion.yrel
                , sdl_event.motion.x, sdl_event.motion.y);
          else if (sdl_event.type == SDL_MOUSEBUTTONDOWN
              || sdl_event.type == SDL_MOUSEBUTTONUP) {
            int key;
            switch (sdl_event.button.button) {
              case SDL_BUTTON_LEFT:   key = 1; break;
              case SDL_BUTTON_MIDDLE: key = 2; break;
              case SDL_BUTTON_RIGHT:  key = 3; break;
              case SDL_BUTTON_X1:     key = 4; break;
              case SDL_BUTTON_X2:     key = 5; break;
              default:                key = -1;
            }
            mousebutton_event_cb(key, sdl_event.type == SDL_MOUSEBUTTONDOWN);
          }
      }

      update_cb(dt, t, this);

      t += dt;
      accumulator -= dt;
    }

    draw_cb(accumulator / dt);

    SDL_GL_SwapWindow(_window);

    { // fps counter
      total_frames++;
      draw_count++;
      if (draw_count == 20) {
        draw_count = 0;
        double seconds_per_frame = get_time_in_seconds() - real_time
          , fps = 1. / seconds_per_frame
          , fpsavg = (double)total_frames / get_time_in_seconds()
          , mspf = seconds_per_frame * 1000.;
        char title[256];
        snprintf(title, 256, "poly2tri | %7.2f ms/frame, %7.2f frames/s, %7.2f "
            "frames/s avg", mspf, fps, fpsavg);
        SDL_SetWindowTitle(_window, title);
      }
    }
  }

  cleanup_cb();
}

inline double screen::get_time_in_seconds() {
  return SDL_GetTicks() / 1000.;
}

