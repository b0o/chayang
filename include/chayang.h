#ifndef CHAYANG_H
#define CHAYANG_H

#include <stdbool.h>
#include <stdint.h>

#include <wayland-client-core.h>

struct chayang {
  struct wl_compositor *compositor;
  struct zwlr_layer_shell_v1 *layer_shell;
  struct wp_viewporter *viewporter;
  struct wp_single_pixel_buffer_manager_v1 *single_pixel_buffer_manager;

  struct wl_list outputs;
  struct wl_list seats;

  bool running, cancelled;
  int64_t delay_ms, start_time_ms;
  bool reverse, exit_on_key;
};

struct chayang_output {
  struct wl_output *wl;
  struct chayang *chayang;
  uint32_t global_name;
  struct wl_list link;

  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *layer_surface;
  struct wp_viewport *viewport;
  struct wl_callback *frame_callback;

  int32_t surface_width, surface_height;
};

struct chayang_seat {
  struct wl_seat *wl;
  struct chayang *chayang;
  uint32_t global_name;
  struct wl_list link;

  uint32_t caps;
  struct wl_pointer *pointer;
  struct wl_keyboard *keyboard;
};

#endif
