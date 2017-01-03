#pragma once

#define GLEW_STATIC
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <queue>

class screen
{
  SDL_Window *_window;
  SDL_GLContext _gl_context;

public:
  int window_width, window_height;
  bool running;

  screen(int n_window_width, int n_window_height);
  ~screen();
  void mainloop(void (*load_cb)(screen*)
      , void (*key_event_cb)(char, bool)
      , void (*mousemotion_event_cb)(float, float, int, int)
      , void (*mousebutton_event_cb)(int, bool)
      , void (*update_cb)(double, double, screen*)
      , void (*draw_cb)(double)
      , void (*cleanup_cb)(void));
  double get_time_in_seconds();
};

