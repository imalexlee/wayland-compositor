#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

enum tm_cursor_mode {
    TM_CURSOR_PASSTHROUGH,
    TM_CURSOR_MOVE,
    TM_CURSOR_RESIZE,

};

struct tm_server {
    struct wl_display*              wl_display;
    struct wl_event_loop*           wl_event_loop;
    struct wl_listener              new_output;
    struct wl_listener              new_input;
    struct wl_listener              new_xdg_top_level;
    struct wl_listener              new_xdg_popup;
    struct wl_listener              cursor_motion;
    struct wl_listener              cursor_motion_abs;
    struct wl_listener              cursor_button;
    struct wl_listener              cursor_axis;
    struct wl_listener              cursor_frame;
    struct wl_listener              request_cursor;
    struct wl_listener              request_set_selection;
    struct wl_list                  outputs;
    struct wl_list                  keyboards;
    struct wl_list                  top_levels;
    struct wlr_data_device_manager* dev_manager;
    struct wlr_compositor*          compositor;
    struct wlr_subcompositor*       subcompositor;
    struct wlr_backend*             backend;
    struct wlr_renderer*            renderer;
    struct wlr_allocator*           allocator;
    struct wlr_output_layout*       output_layout;
    struct wlr_scene*               scene;
    struct wlr_scene_output_layout* scene_layout;
    struct wlr_seat*                seat;
    struct wlr_cursor*              cursor;
    struct wlr_xcursor_manager*     cursor_mgr;
    struct wlr_box                  grab_geobox;
    struct wlr_xdg_shell*           xdg_shell;
    struct tm_top_level*            grabbed_top_level;
    enum tm_cursor_mode             cursor_mode;
    double                          grab_x;
    double                          grab_y;
    uint32_t                        resize_edges;
};

struct tm_output {
    struct wl_listener destroy;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_list     link;
    struct wlr_output* wlr_output;
    struct tm_server*  server;
    struct timespec    last_frame;
};

struct tm_top_level {
    struct wl_list           link;
    struct wlr_xdg_toplevel* xdg_top_level;
    struct wlr_scene_tree*   scene_tree;
    struct wl_listener       map;
    struct wl_listener       unmap;
    struct wl_listener       commit;
    struct wl_listener       destroy;
    struct wl_listener       request_move;
    struct wl_listener       request_resize;
    struct wl_listener       request_maximize;
    struct wl_listener       request_fullscreen;
    struct tm_server*        server;
};

struct tm_popup {
    struct wl_listener    commit;
    struct wl_listener    destroy;
    struct wlr_xdg_popup* xdg_popup;
};

struct tm_keyboard {
    struct wl_listener   modifiers;
    struct wl_listener   key;
    struct wl_listener   destroy;
    struct wl_list       link;
    struct wlr_keyboard* wlr_keyboard;
    struct tm_server*    server;
};

static void server_new_output(struct wl_listener* listener, void* data);
static void output_destroy(struct wl_listener* listener, void* data);
static void output_request_state(struct wl_listener* listener, void* data);
static void output_frame(struct wl_listener* listener, void* data);

static void server_new_xdg_top_level(struct wl_listener* listener, void* data);
static void xdg_top_level_map(struct wl_listener* listener, void* data);
static void xdg_top_level_unmap(struct wl_listener* listener, void* data);
static void xdg_top_level_commit(struct wl_listener* listener, void* data);
static void xdg_top_level_destroy(struct wl_listener* listener, void* data);
static void xdg_top_level_request_move(struct wl_listener* listener, void* data);
static void xdg_top_level_request_resize(struct wl_listener* listener, void* data);
static void xdg_top_level_request_maximize(struct wl_listener* listener, void* data);
static void xdg_top_level_request_fullscreen(struct wl_listener* listener, void* data);

static void server_new_xdg_popup(struct wl_listener* listener, void* data);
static void xdg_popup_commit(struct wl_listener* listener, void* data);
static void xdg_popup_destroy(struct wl_listener* listener, void* data);

static void server_cursor_motion(struct wl_listener* listener, void* data);
static void server_cursor_motion_absolute(struct wl_listener* listener, void* data);
static void server_cursor_button(struct wl_listener* listener, void* data);
static void server_cursor_axis(struct wl_listener* listener, void* data);
static void server_cursor_frame(struct wl_listener* listener, void* data);

static void server_new_input(struct wl_listener* listener, void* data);
static void seat_request_cursor(struct wl_listener* listener, void* data);
static void seat_request_set_selection(struct wl_listener* listener, void* data);

static void keyboard_handle_modifiers(struct wl_listener* listener, void* data);
static void keyboard_handle_key(struct wl_listener* listener, void* data);
static void keyboard_handle_destroy(struct wl_listener* listener, void* data);

static void focus_top_level(struct tm_top_level* top_level, struct wlr_surface* surface);
static void reset_cursor_mode(struct tm_server* server);
static void
begin_interactive(struct tm_top_level* toplevel, enum tm_cursor_mode mode, uint32_t edges);
static void process_cursor_motion(struct tm_server* server, uint32_t time);
static void process_cursor_move(struct tm_server* server);
static void process_cursor_resize(struct tm_server* server);

static struct tm_top_level* desktop_top_level_at(struct tm_server*    server,
                                                 double               lx,
                                                 double               ly,
                                                 struct wlr_surface** surface,
                                                 double*              sx,
                                                 double*              sy);
static void server_new_keyboard(struct tm_server* server, struct wlr_input_device* device);
static void server_new_pointer(struct tm_server* server, struct wlr_input_device* device);
static bool handle_keybinding(struct tm_server* server, xkb_keysym_t sym);

int main() {

    struct tm_server server;
    server.wl_display    = wl_display_create();
    server.wl_event_loop = wl_display_get_event_loop(server.wl_display);
    server.backend       = wlr_backend_autocreate(server.wl_event_loop, NULL);
    server.renderer      = wlr_renderer_autocreate(server.backend);

    assert(server.wl_display && server.backend && server.renderer);

    if (!wlr_renderer_init_wl_display(server.renderer, server.wl_display)) {
        return 1;
    }

    server.allocator     = wlr_allocator_autocreate(server.backend, server.renderer);
    server.compositor    = wlr_compositor_create(server.wl_display, 5, server.renderer);
    server.subcompositor = wlr_subcompositor_create(server.wl_display);
    server.dev_manager   = wlr_data_device_manager_create(server.wl_display);
    server.output_layout = wlr_output_layout_create(server.wl_display);

    wl_list_init(&server.outputs);
    server.new_output.notify = server_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    server.scene        = wlr_scene_create();
    server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

    wl_list_init(&server.top_levels);
    server.xdg_shell                = wlr_xdg_shell_create(server.wl_display, 3);
    server.new_xdg_top_level.notify = server_new_xdg_top_level;
    server.new_xdg_popup.notify     = server_new_xdg_popup;

    wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_top_level);
    wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

    server.cursor_mgr               = wlr_xcursor_manager_create(NULL, 24);
    server.cursor_mode              = TM_CURSOR_PASSTHROUGH;
    server.cursor_motion.notify     = server_cursor_motion;
    server.cursor_motion_abs.notify = server_cursor_motion_absolute;
    server.cursor_button.notify     = server_cursor_button;
    server.cursor_axis.notify       = server_cursor_axis;
    server.cursor_frame.notify      = server_cursor_frame;

    wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
    wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_abs);
    wl_signal_add(&server.cursor->events.button, &server.cursor_button);
    wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
    wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

    server.seat = wlr_seat_create(server.wl_display, "seat0");

    wl_list_init(&server.keyboards);
    server.new_input.notify             = server_new_input;
    server.request_cursor.notify        = seat_request_cursor;
    server.request_set_selection.notify = seat_request_set_selection;

    wl_signal_add(&server.backend->events.new_input, &server.new_input);
    wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
    wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);

    const char* socket = wl_display_add_socket_auto(server.wl_display);
    if (!socket) {
        wlr_backend_destroy(server.backend);
        return 1;
    }

    if (!wlr_backend_start(server.backend)) {
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }
    setenv("WAYLAND_DISPLAY", socket, true);

    printf("Running Wayland compositor on WAYLAND_DISPLAY=%s\n", socket);
    wl_display_run(server.wl_display);

    wl_display_destroy_clients(server.wl_display);
    wlr_scene_node_destroy(&server.scene->tree.node);
    wlr_xcursor_manager_destroy(server.cursor_mgr);
    wlr_cursor_destroy(server.cursor);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);
    return 0;
}

static void server_new_output(struct wl_listener* listener, void* data) {

    struct tm_server* server = wl_container_of(listener, server, new_output);

    struct wlr_output* wlr_output = data;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
    if (mode != NULL) {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    struct tm_output* output     = calloc(1, sizeof(struct tm_output));
    output->server               = server;
    output->wlr_output           = wlr_output;
    output->frame.notify         = output_frame;
    output->request_state.notify = output_request_state;
    output->destroy.notify       = output_destroy;

    wl_signal_add(&wlr_output->events.frame, &output->frame);
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wl_list_insert(&server->outputs, &output->link);

    struct wlr_output_layout_output* layout_output =
        wlr_output_layout_add_auto(server->output_layout, wlr_output);
    struct wlr_scene_output* scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, layout_output, scene_output);
}

static void output_frame(struct wl_listener* listener, [[maybe_unused]] void* data) {
    struct tm_output* output = wl_container_of(listener, output, frame);

    struct wlr_scene* scene = output->server->scene;

    struct wlr_scene_output* scene_output = wlr_scene_get_scene_output(scene, output->wlr_output);
    wlr_scene_output_commit(scene_output, NULL);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener* listener, void* data) {
    struct tm_output* output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state* event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener* listener, [[maybe_unused]] void* data) {
    struct tm_output* output = wl_container_of(listener, output, destroy);
    wl_list_remove(&output->link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->frame.link);
    free(output);
}

static void server_new_xdg_top_level(struct wl_listener* listener, void* data) {
    struct tm_server* server = wl_container_of(listener, server, new_xdg_top_level);

    struct wlr_xdg_toplevel* xdg_top_level = data;

    struct tm_top_level* top_level = calloc(1, sizeof(struct tm_top_level));
    top_level->server              = server;
    top_level->xdg_top_level       = xdg_top_level;
    top_level->scene_tree =
        wlr_scene_xdg_surface_create(&top_level->server->scene->tree, xdg_top_level->base);
    top_level->scene_tree->node.data = top_level;
    xdg_top_level->base->data        = top_level->scene_tree;

    // listen to shell window events
    top_level->map.notify                = xdg_top_level_map;
    top_level->unmap.notify              = xdg_top_level_unmap;
    top_level->commit.notify             = xdg_top_level_commit;
    top_level->destroy.notify            = xdg_top_level_destroy;
    top_level->request_move.notify       = xdg_top_level_request_move;
    top_level->request_resize.notify     = xdg_top_level_request_resize;
    top_level->request_maximize.notify   = xdg_top_level_request_maximize;
    top_level->request_fullscreen.notify = xdg_top_level_request_fullscreen;

    wl_signal_add(&xdg_top_level->base->surface->events.map, &top_level->map);
    wl_signal_add(&xdg_top_level->base->surface->events.unmap, &top_level->unmap);
    wl_signal_add(&xdg_top_level->base->surface->events.commit, &top_level->commit);
    wl_signal_add(&xdg_top_level->base->surface->events.destroy, &top_level->destroy);
    wl_signal_add(&xdg_top_level->events.request_move, &top_level->request_move);
    wl_signal_add(&xdg_top_level->events.request_resize, &top_level->request_resize);
    wl_signal_add(&xdg_top_level->events.request_maximize, &top_level->request_maximize);
    wl_signal_add(&xdg_top_level->events.request_fullscreen, &top_level->request_fullscreen);
}

// surface is ready to display
static void xdg_top_level_map(struct wl_listener* listener, [[maybe_unused]] void* data) {
    struct tm_top_level* top_level = wl_container_of(listener, top_level, map);
    wl_list_insert(&top_level->server->top_levels, &top_level->link);
    focus_top_level(top_level, top_level->xdg_top_level->base->surface);
}

// surface should no longer be shown
static void xdg_top_level_unmap(struct wl_listener* listener, [[maybe_unused]] void* data) {
    struct tm_top_level* top_level = wl_container_of(listener, top_level, unmap);
    if (top_level == top_level->server->grabbed_top_level) {
        reset_cursor_mode(top_level->server);
    }
    wl_list_remove(&top_level->link);
}

// new surface state is committed
static void xdg_top_level_commit(struct wl_listener* listener, [[maybe_unused]] void* data) {
    struct tm_top_level* top_level = wl_container_of(listener, top_level, commit);

    if (top_level->xdg_top_level->base->initial_commit) {
        wlr_xdg_toplevel_set_size(top_level->xdg_top_level, 0, 0);
    }
}

static void xdg_top_level_destroy(struct wl_listener* listener, [[maybe_unused]] void* data) {
    struct tm_top_level* top_level = wl_container_of(listener, top_level, destroy);

    wl_list_remove(&top_level->map.link);
    wl_list_remove(&top_level->unmap.link);
    wl_list_remove(&top_level->commit.link);
    wl_list_remove(&top_level->destroy.link);
    wl_list_remove(&top_level->request_move.link);
    wl_list_remove(&top_level->request_resize.link);
    wl_list_remove(&top_level->request_maximize.link);
    wl_list_remove(&top_level->request_fullscreen.link);

    free(top_level);
}

static void xdg_top_level_request_move(struct wl_listener* listener, [[maybe_unused]] void* data) {
    struct tm_top_level* top_level = wl_container_of(listener, top_level, request_move);
    begin_interactive(top_level, TM_CURSOR_MOVE, 0);
}

static void xdg_top_level_request_resize(struct wl_listener* listener, void* data) {
    struct wlr_xdg_toplevel_resize_event* event = data;
    struct tm_top_level* top_level = wl_container_of(listener, top_level, request_resize);
    begin_interactive(top_level, TM_CURSOR_RESIZE, event->edges);
}

static void xdg_top_level_request_maximize(struct wl_listener*    listener,
                                           [[maybe_unused]] void* data) {
    struct tm_top_level* top_level = wl_container_of(listener, top_level, request_maximize);
    if (top_level->xdg_top_level->base->initialized) {
        wlr_xdg_surface_schedule_configure(top_level->xdg_top_level->base);
    }
}

static void xdg_top_level_request_fullscreen(struct wl_listener*    listener,
                                             [[maybe_unused]] void* data) {
    struct tm_top_level* toplevel = wl_container_of(listener, toplevel, request_fullscreen);
    if (toplevel->xdg_top_level->base->initialized) {
        wlr_xdg_surface_schedule_configure(toplevel->xdg_top_level->base);
    }
}

static void server_new_xdg_popup([[maybe_unused]] struct wl_listener* listener, void* data) {
    struct wlr_xdg_popup*   xdg_popup = data;
    struct wlr_xdg_surface* parent    = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    assert(parent);

    struct wlr_scene_tree* parent_tree = parent->data;
    xdg_popup->base->data              = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    struct tm_popup* popup = calloc(1, sizeof(struct tm_popup));
    popup->xdg_popup       = xdg_popup;
    popup->commit.notify   = xdg_popup_commit;
    popup->destroy.notify  = xdg_popup_destroy;

    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

static void xdg_popup_commit(struct wl_listener* listener, [[maybe_unused]] void* data) {
    struct tm_popup* popup = wl_container_of(listener, popup, commit);
    if (popup->xdg_popup->base->initial_commit) {
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
    }
}

static void xdg_popup_destroy(struct wl_listener* listener, [[maybe_unused]] void* data) {
    struct tm_popup* popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);

    free(popup);
}

// pointer emits a relatve pointer motion event like a delta
static void server_cursor_motion(struct wl_listener* listener, void* data) {
    struct tm_server*                server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event* event  = data;

    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener* listener, void* data) {
    struct tm_server* server = wl_container_of(listener, server, cursor_motion_abs);
    struct wlr_pointer_motion_absolute_event* event = data;

    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
}

// pointer emits a button event
static void server_cursor_button(struct wl_listener* listener, void* data) {
    struct tm_server*                server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event* event  = data;

    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);

    double               sx, sy;
    struct wlr_surface*  surface = NULL;
    struct tm_top_level* top_level =
        desktop_top_level_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        reset_cursor_mode(server);
    } else {
        // focus that client if the button was pressed
        focus_top_level(top_level, surface);
    }
}

// cursor forwards this when pointer emits an axis event like moving a scroll wheel
static void server_cursor_axis(struct wl_listener* listener, void* data) {
    struct tm_server*              server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event* event  = data;

    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta,
                                 event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener* listener, [[maybe_unused]] void* data) {
    struct tm_server* server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void server_new_input(struct wl_listener* listener, void* data) {
    struct tm_server*        server = wl_container_of(listener, server, new_input);
    struct wlr_input_device* device = data;
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        server_new_keyboard(server, device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        server_new_pointer(server, device);
        break;
    default:
        break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat, caps);
}

// client provides a cursor image
static void seat_request_cursor(struct wl_listener* listener, void* data) {
    struct tm_server* server = wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event* event = data;
    struct wlr_seat_client* focused_client = server->seat->pointer_state.focused_client;

    if (focused_client == event->seat_client) {
        wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

static void seat_request_set_selection(struct wl_listener* listener, void* data) {
    struct tm_server* server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event* event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

// modifier like shift or alt is pressed
static void keyboard_handle_modifiers(struct wl_listener* listener, [[maybe_unused]] void* data) {
    struct tm_keyboard* keyboard = wl_container_of(listener, keyboard, modifiers);

    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener* listener, void* data) {
    struct tm_keyboard*            keyboard = wl_container_of(listener, keyboard, key);
    struct tm_server*              server   = keyboard->server;
    struct wlr_keyboard_key_event* event    = data;
    struct wlr_seat*               seat     = server->seat;

    const xkb_keysym_t* syms;

    uint32_t keycode = event->keycode + 8;
    int      nsyms   = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool     handled   = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
    if ((modifiers & WLR_MODIFIER_ALT) && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            handled = handle_keybinding(server, syms[i]);
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener* listener, [[maybe_unused]] void* data) {
    struct tm_keyboard* keyboard = wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

// for keyboard focus
static void focus_top_level(struct tm_top_level* top_level, struct wlr_surface* surface) {
    if (top_level == NULL) {
        return;
    }

    struct tm_server*   server       = top_level->server;
    struct wlr_seat*    seat         = server->seat;
    struct wlr_surface* prev_surface = seat->keyboard_state.focused_surface;

    if (prev_surface == surface) {
        // don't refocus an already focused surface
        return;
    }

    if (prev_surface) {
        // deactivate previously focused surface
        struct wlr_xdg_toplevel* prev_top_level =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_top_level) {
            wlr_xdg_toplevel_set_activated(prev_top_level, false);
        }
    }

    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat);

    // move top-level surface to the front
    wlr_scene_node_raise_to_top(&top_level->scene_tree->node);
    // unlink surface from current position in scene list
    wl_list_remove(&top_level->link);
    wl_list_insert(&server->top_levels, &top_level->link);
    wlr_xdg_toplevel_set_activated(top_level->xdg_top_level, true);

    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, top_level->xdg_top_level->base->surface,
                                       keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }
}

static void reset_cursor_mode(struct tm_server* server) {
    server->cursor_mode       = TM_CURSOR_PASSTHROUGH;
    server->grabbed_top_level = NULL;
}

// sets up interactive move or resize operation
static void
begin_interactive(struct tm_top_level* top_level, enum tm_cursor_mode mode, uint32_t edges) {
    struct tm_server*   server          = top_level->server;
    struct wlr_surface* focused_surface = server->seat->pointer_state.focused_surface;
    if (top_level->xdg_top_level->base->surface != wlr_surface_get_root_surface(focused_surface)) {
        // trying to move or resize unfocused clients
        return;
    }

    server->grabbed_top_level = top_level;
    server->cursor_mode       = mode;

    if (mode == TM_CURSOR_MOVE) {
        server->grab_x = server->cursor->x - top_level->scene_tree->node.x;
        server->grab_y = server->cursor->y - top_level->scene_tree->node.y;
    } else {
        struct wlr_box* geo_box = &top_level->xdg_top_level->base->geometry;

        double border_x = (top_level->scene_tree->node.x + geo_box->x) +
                          ((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
        double border_y = (top_level->scene_tree->node.y + geo_box->y) +
                          ((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);

        server->grab_x      = server->cursor->x - border_x;
        server->grab_y      = server->cursor->y - border_y;
        server->grab_geobox = *geo_box;
        server->grab_geobox.x += top_level->scene_tree->node.x;
        server->grab_geobox.y += top_level->scene_tree->node.y;
        server->resize_edges = edges;
    }
}

static void process_cursor_motion(struct tm_server* server, uint32_t time) {
    if (server->cursor_mode == TM_CURSOR_MOVE) {
        process_cursor_move(server);
        return;
    } else if (server->cursor_mode == TM_CURSOR_RESIZE) {
        process_cursor_resize(server);
        return;
    }

    double               sx, sy;
    struct wlr_seat*     seat    = server->seat;
    struct wlr_surface*  surface = NULL;
    struct tm_top_level* top_level =
        desktop_top_level_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (!top_level) {
        // if cursor is not over a top_level node, set the cursor to default
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }

    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(seat);
    }
}

static void process_cursor_move(struct tm_server* server) {
    struct tm_top_level* top_level = server->grabbed_top_level;
    wlr_scene_node_set_position(&top_level->scene_tree->node, server->cursor->x - server->grab_x,
                                server->cursor->y - server->grab_y);
}

static void process_cursor_resize(struct tm_server* server) {

    struct tm_top_level* toplevel = server->grabbed_top_level;

    double border_x   = server->cursor->x - server->grab_x;
    double border_y   = server->cursor->y - server->grab_y;
    int    new_left   = server->grab_geobox.x;
    int    new_right  = server->grab_geobox.x + server->grab_geobox.width;
    int    new_top    = server->grab_geobox.y;
    int    new_bottom = server->grab_geobox.y + server->grab_geobox.height;

    if (server->resize_edges & WLR_EDGE_TOP) {
        new_top = border_y;
        if (new_top >= new_bottom) {
            new_top = new_bottom - 1;
        }
    } else if (server->resize_edges & WLR_EDGE_BOTTOM) {
        new_bottom = border_y;
        if (new_bottom <= new_top) {
            new_bottom = new_top + 1;
        }
    }
    if (server->resize_edges & WLR_EDGE_LEFT) {
        new_left = border_x;
        if (new_left >= new_right) {
            new_left = new_right - 1;
        }
    } else if (server->resize_edges & WLR_EDGE_RIGHT) {
        new_right = border_x;
        if (new_right <= new_left) {
            new_right = new_left + 1;
        }
    }

    struct wlr_box* geo_box = &toplevel->xdg_top_level->base->geometry;
    wlr_scene_node_set_position(&toplevel->scene_tree->node, new_left - geo_box->x,
                                new_top - geo_box->y);

    int new_width  = new_right - new_left;
    int new_height = new_bottom - new_top;
    wlr_xdg_toplevel_set_size(toplevel->xdg_top_level, new_width, new_height);
}

static struct tm_top_level* desktop_top_level_at(struct tm_server*    server,
                                                 double               lx,
                                                 double               ly,
                                                 struct wlr_surface** surface,
                                                 double*              sx,
                                                 double*              sy) {

    struct wlr_scene_node* node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }
    struct wlr_scene_buffer*  scene_buffer  = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface* scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }

    *surface = scene_surface->surface;

    struct wlr_scene_tree* tree = node->parent;
    while (tree != NULL && tree->node.data == NULL) {
        tree = tree->node.parent;
    }
    return tree->node.data;
}

static void server_new_keyboard(struct tm_server* server, struct wlr_input_device* device) {
    struct wlr_keyboard* wlr_keyboard = wlr_keyboard_from_input_device(device);

    struct tm_keyboard* keyboard = calloc(1, sizeof(*keyboard));
    keyboard->server             = server;
    keyboard->wlr_keyboard       = wlr_keyboard;

    struct xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap*  keymap =
        xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

    keyboard->modifiers.notify = keyboard_handle_modifiers;
    keyboard->key.notify       = keyboard_handle_key;
    keyboard->destroy.notify   = keyboard_handle_destroy;

    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

    wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct tm_server* server, struct wlr_input_device* device) {
    wlr_cursor_attach_input_device(server->cursor, device);
}

static bool handle_keybinding(struct tm_server* server, xkb_keysym_t sym) {
    switch (sym) {
    case XKB_KEY_Escape:
        wl_display_terminate(server->wl_display);
        break;
    case XKB_KEY_F1:
        if (wl_list_length(&server->top_levels) < 2) {
            break;
        }
        struct tm_top_level* next_toplevel =
            wl_container_of(server->top_levels.prev, next_toplevel, link);
        focus_top_level(next_toplevel, next_toplevel->xdg_top_level->base->surface);
        break;
    default:
        return false;
    }
    return true;
}
