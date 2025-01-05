#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-cursor.h>

#include "chayang.h"
#include "single-pixel-buffer-v1-protocol.h"
#include "viewporter-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

static void repaint_output(struct chayang_output *output);

static int64_t now_ms(void) {
  struct timespec ts = {0};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    perror("clock_gettime() failed");
    exit(1);
  }
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void cancel(struct chayang *state) {
  state->running = false;
  state->cancelled = true;
}

static void frame_callback_handle_done(void *data, struct wl_callback *callback,
                                       uint32_t time) {
  struct chayang_output *output = data;
  assert(callback == output->frame_callback);
  wl_callback_destroy(callback);
  output->frame_callback = NULL;
  repaint_output(output);
}

static const struct wl_callback_listener frame_callback_listener = {
    .done = frame_callback_handle_done,
};

static void repaint_output(struct chayang_output *output) {
  int64_t delta = now_ms() - output->chayang->start_time_ms;
  double progress = (double)delta / output->chayang->delay_ms;
  if (progress >= 1) {
    output->chayang->running = false;
    return;
  }

  uint32_t alpha = progress * UINT32_MAX;
  struct wl_buffer *buffer =
      wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(
          output->chayang->single_pixel_buffer_manager, 0, 0, 0, alpha);
  wl_surface_attach(output->surface, buffer, 0, 0);

  wp_viewport_set_destination(output->viewport, output->surface_width,
                              output->surface_height);

  if (output->frame_callback != NULL) {
    wl_callback_destroy(output->frame_callback);
  }
  output->frame_callback = wl_surface_frame(output->surface);
  wl_callback_add_listener(output->frame_callback, &frame_callback_listener,
                           output);

  wl_surface_damage(output->surface, 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(output->surface);

  wl_buffer_destroy(buffer);
}

static void destroy_output(struct chayang_output *output) {
  if (output->frame_callback != NULL) {
    wl_callback_destroy(output->frame_callback);
  }
  wp_viewport_destroy(output->viewport);
  zwlr_layer_surface_v1_destroy(output->layer_surface);
  wl_surface_destroy(output->surface);
  wl_list_remove(&output->link);
  wl_output_destroy(output->wl);
  free(output);
}

static void destroy_seat(struct chayang_seat *seat) {
  wl_list_remove(&seat->link);
  if (seat->pointer != NULL) {
    wl_pointer_destroy(seat->pointer);
  }
  if (seat->keyboard != NULL) {
    wl_keyboard_destroy(seat->keyboard);
  }
  wl_seat_destroy(seat->wl);
  free(seat);
}

static void layer_surface_handle_configure(
    void *data, struct zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t width, uint32_t height) {
  struct chayang_output *output = data;

  output->surface_width = width;
  output->surface_height = height;

  zwlr_layer_surface_v1_ack_configure(surface, serial);
  repaint_output(output);
}

static void layer_surface_handle_closed(void *data,
                                        struct zwlr_layer_surface_v1 *surface) {
  struct chayang_output *output = data;
  destroy_output(output);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_handle_configure,
    .closed = layer_surface_handle_closed,
};

static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
                                 uint32_t serial, struct wl_surface *surface,
                                 wl_fixed_t surface_x, wl_fixed_t surface_y) {
  // No-op
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
                                 uint32_t serial, struct wl_surface *surface) {
  // No-op
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t time, wl_fixed_t surface_x,
                                  wl_fixed_t surface_y) {
  struct chayang_seat *seat = data;
  cancel(seat->chayang);
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t serial, uint32_t time,
                                  uint32_t button, uint32_t state) {
  struct chayang_seat *seat = data;
  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    cancel(seat->chayang);
  }
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
                                uint32_t time, uint32_t axis,
                                wl_fixed_t value) {
  struct chayang_seat *seat = data;
  cancel(seat->chayang);
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
};

static void keyboard_handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
                                   uint32_t format, int fd, uint32_t size) {
  close(fd);
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *wl_keyboard,
                                  uint32_t serial,
                                  struct wl_surface *wl_surface,
                                  struct wl_array *keys) {
  // No-op
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *wl_keyboard,
                                  uint32_t serial,
                                  struct wl_surface *wl_surface) {
  // No-op
}

static void keyboard_handle_key(void *data, struct wl_keyboard *wl_keyboard,
                                uint32_t serial, uint32_t time, uint32_t key,
                                uint32_t state) {
  struct chayang_seat *seat = data;
  if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    cancel(seat->chayang);
  }
}

static void keyboard_handle_modifiers(void *data,
                                      struct wl_keyboard *wl_keyboard,
                                      uint32_t serial, uint32_t mods_depressed,
                                      uint32_t mods_latched,
                                      uint32_t mods_locked, uint32_t group) {
  //  No-op
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
};

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
                                     uint32_t caps) {
  struct chayang_seat *seat = data;

  uint32_t removed_caps = seat->caps & ~caps;
  uint32_t added_caps = caps & ~seat->caps;

  if (removed_caps & WL_SEAT_CAPABILITY_POINTER) {
    wl_pointer_destroy(seat->pointer);
    seat->pointer = NULL;
  }
  if (removed_caps & WL_SEAT_CAPABILITY_KEYBOARD) {
    wl_keyboard_destroy(seat->keyboard);
    seat->keyboard = NULL;
  }

  if (added_caps & WL_SEAT_CAPABILITY_POINTER) {
    seat->pointer = wl_seat_get_pointer(seat->wl);
    wl_pointer_add_listener(seat->pointer, &pointer_listener, seat);
  }
  if (added_caps & WL_SEAT_CAPABILITY_KEYBOARD) {
    seat->keyboard = wl_seat_get_keyboard(seat->wl);
    wl_keyboard_add_listener(seat->keyboard, &keyboard_listener, seat);
  }

  seat->caps = caps;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
  struct chayang *state = data;

  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    state->compositor =
        wl_registry_bind(registry, name, &wl_compositor_interface, 4);
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    state->layer_shell =
        wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
  } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
    state->viewporter =
        wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
  } else if (strcmp(interface,
                    wp_single_pixel_buffer_manager_v1_interface.name) == 0) {
    state->single_pixel_buffer_manager = wl_registry_bind(
        registry, name, &wp_single_pixel_buffer_manager_v1_interface, 1);
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    struct chayang_output *output = calloc(1, sizeof(*output));
    output->global_name = name;
    output->chayang = state;
    output->wl = wl_registry_bind(registry, name, &wl_output_interface, 1);
    wl_list_insert(&state->outputs, &output->link);
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    struct chayang_seat *seat = calloc(1, sizeof(*seat));
    seat->global_name = name;
    seat->chayang = state;
    seat->wl = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    wl_seat_add_listener(seat->wl, &seat_listener, seat);
    wl_list_insert(&state->seats, &seat->link);
  }
}

static void registry_handle_global_remove(void *data,
                                          struct wl_registry *registry,
                                          uint32_t name) {
  struct chayang *state = data;

  struct chayang_output *output;
  wl_list_for_each(output, &state->outputs, link) {
    if (output->global_name == name) {
      destroy_output(output);
      break;
    }
  }

  struct chayang_seat *seat;
  wl_list_for_each(seat, &state->seats, link) {
    if (seat->global_name == name) {
      destroy_seat(seat);
      break;
    }
  }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

int main(int argc, char *argv[]) {
  struct chayang state = {0};
  wl_list_init(&state.outputs);
  wl_list_init(&state.seats);

  double delay_sec = 3;
  while (1) {
    int opt = getopt(argc, argv, "hd:");
    if (opt < 0) {
      break;
    }

    switch (opt) {
    case 'd':;
      char *end = NULL;
      errno = 0;
      delay_sec = strtod(optarg, &end);
      if (errno != 0 || end == optarg || end != &optarg[strlen(optarg)]) {
        fprintf(stderr, "invalid -d value\n");
        return 1;
      }
      break;
    default:
      fprintf(stderr, "usage: chayang [-d seconds]\n");
      return opt == 'h' ? 0 : 1;
    }
  }

  state.delay_ms = delay_sec * 1000;

  struct wl_display *display = wl_display_connect(NULL);
  if (display == NULL) {
    fprintf(stderr, "failed to connect to display\n");
    return 1;
  }

  struct wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, &state);

  if (wl_display_roundtrip(display) < 0) {
    fprintf(stderr, "wl_display_roundtrip() failed\n");
    return 1;
  }

  struct {
    const char *name;
    bool found;
  } required_globals[] = {
      {"wl_compositor", state.compositor != NULL},
      {"zwlr_layer_shell_v1", state.layer_shell != NULL},
      {"wp_viewporter", state.viewporter != NULL},
      {"wp_single_pixel_buffer_manager_v1",
       state.single_pixel_buffer_manager != NULL},
  };
  for (size_t i = 0; i < sizeof(required_globals) / sizeof(required_globals[0]);
       i++) {
    if (!required_globals[i].found) {
      fprintf(stderr, "missing %s global\n", required_globals[i].name);
      return 1;
    }
  }

  struct chayang_output *output;
  wl_list_for_each(output, &state.outputs, link) {
    output->surface = wl_compositor_create_surface(state.compositor);

    output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        state.layer_shell, output->surface, output->wl,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "dim");
    zwlr_layer_surface_v1_add_listener(output->layer_surface,
                                       &layer_surface_listener, output);

    output->viewport =
        wp_viewporter_get_viewport(state.viewporter, output->surface);

    zwlr_layer_surface_v1_set_anchor(output->layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);
    zwlr_layer_surface_v1_set_keyboard_interactivity(output->layer_surface,
                                                     true);
    zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
    wl_surface_commit(output->surface);
  }

  int ret = 0;
  state.running = true;
  state.start_time_ms = now_ms();
  while (state.running) {
    if (wl_display_dispatch(display) < 0) {
      fprintf(stderr, "wl_display_dispatch() failed\n");
      ret = 1;
      break;
    }
  }
  if (ret == 0 && state.cancelled) {
    ret = 2;
  }

  struct chayang_output *output_tmp;
  wl_list_for_each_safe(output, output_tmp, &state.outputs, link) {
    destroy_output(output);
  }

  struct chayang_seat *seat, *seat_tmp;
  wl_list_for_each_safe(seat, seat_tmp, &state.seats, link) {
    destroy_seat(seat);
  }

  wl_compositor_destroy(state.compositor);
  zwlr_layer_shell_v1_destroy(state.layer_shell);
  wp_viewporter_destroy(state.viewporter);
  wp_single_pixel_buffer_manager_v1_destroy(state.single_pixel_buffer_manager);
  wl_registry_destroy(registry);
  wl_display_disconnect(display);

  return ret;
}
