#include <drm_fourcc.h>
#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
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
#include <wlr/types/wlr_layer_shell_v1.h>
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include <wlr/types/wlr_screencopy_v1.h>
/* xdg-output: exposes logical output name/geometry to clients */
#include <wlr/types/wlr_xdg_output_v1.h>
/* output-management: lets clients read/configure outputs (resolution, refresh, position) */
#include <wlr/types/wlr_output_management_v1.h>
/* output-power-management: DPMS / screen blanking */
#include <wlr/types/wlr_output_power_management_v1.h>
/* pointer-constraints: pointer lock + confinement for games / 3D apps */
#include <wlr/types/wlr_pointer_constraints_v1.h>
/* relative-pointer: delta motion events, companion to pointer-constraints */
#include <wlr/types/wlr_relative_pointer_v1.h>
/* cursor-shape: protocol-level named cursor shapes, no surface needed */
#include <wlr/types/wlr_cursor_shape_v1.h>
/* fractional-scale: non-integer HiDPI scaling (1.5x, 1.25x, etc.) */
#include <wlr/types/wlr_fractional_scale_v1.h>
/* viewporter: lets clients crop/scale their surface buffers */
#include <wlr/types/wlr_viewporter.h>
/* foreign-toplevel-management: exposes window list/state to taskbars */
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
/* idle-inhibit: lets apps (video players) suppress idle/screen-blank */
#include <wlr/types/wlr_idle_inhibit_v1.h>
/* virtual-keyboard: synthetic keyboard input (OSK, automation) */
#include <wlr/types/wlr_virtual_keyboard_v1.h>
/* virtual-pointer: synthetic pointer input */
#include <wlr/types/wlr_virtual_pointer_v1.h>
/* single-pixel-buffer: efficient 1x1 solid-color buffers (GTK4 backgrounds) */
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
/* data-control: programmatic clipboard read/write (wl-clipboard, clipboard managers) */
#include <wlr/types/wlr_data_control_v1.h>
/* xdg-decoration: server-side vs client-side decoration negotiation */
#include <wlr/types/wlr_xdg_decoration_v1.h>
/* tearing-control: lets clients opt into tearing for lower latency (games) */
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

/* Forward declarations */
struct tinywl_server;
static void server_update_usable_area(struct tinywl_server *server);

/* For brevity's sake, struct members are annotated where they are used. */
enum tinywl_cursor_mode {
	TINYWL_CURSOR_PASSTHROUGH,
	TINYWL_CURSOR_MOVE,
	TINYWL_CURSOR_RESIZE,
};

struct tinywl_config {
	int gaps_in;
	int gaps_out;
	int border_width;
	uint32_t border_active;
	uint32_t border_inactive;
};

struct tinywl_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;
    struct wlr_screencopy_manager_v1 *screencopy_manager;

	/* xdg-output-v1: logical output name/geometry for multi-monitor apps */
	struct wlr_xdg_output_manager_v1 *xdg_output_manager;

	/* wlr-output-management-v1: read/configure outputs (wlr-randr, settings app) */
	struct wlr_output_manager_v1 *output_manager;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;

	/* wlr-output-power-management-v1: DPMS / screen blanking */
	struct wlr_output_power_manager_v1 *output_power_manager;
	struct wl_listener output_power_set_mode;

	/* zwp-pointer-constraints-v1: pointer lock + confinement */
	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wl_listener new_pointer_constraint;

	/* zwp-relative-pointer-manager-v1: delta motion events for games/3D */
	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;

	/* wp-cursor-shape-manager-v1: protocol-level named cursor shapes */
	struct wlr_cursor_shape_manager_v1 *cursor_shape_manager;
	struct wl_listener request_set_shape;

	/* wp-fractional-scale-manager-v1: non-integer HiDPI scaling */
	struct wlr_fractional_scale_manager_v1 *fractional_scale_manager;

	/* wp-viewporter: crop/scale surface buffers */
	struct wlr_viewporter *viewporter;

	/* wlr-foreign-toplevel-management-v1: exposes window list and state
	 * (title, app_id, maximized, fullscreen, minimized) to external clients.
	 * The lume taskbar uses this to show window buttons and control windows. */
	struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;

	/* zwp-idle-inhibit-manager-v1: lets apps suppress idle/screen-blank.
	 * Video players, presentation apps, and games create inhibitors via this. */
	struct wlr_idle_inhibit_manager_v1 *idle_inhibit_manager;
	struct wl_listener new_idle_inhibitor;

	/* wlr-virtual-keyboard-v1: synthetic keyboard input.
	 * Used by on-screen keyboards (wvkbd) and automation/accessibility tools. */
	struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_manager;
	struct wl_listener new_virtual_keyboard;

	/* zwp-virtual-pointer-manager-v1: synthetic pointer input.
	 * Used by screen recording tools, accessibility software, and wl-mirror. */
	struct wlr_virtual_pointer_manager_v1 *virtual_pointer_manager;
	struct wl_listener new_virtual_pointer;

	/* wp-single-pixel-buffer-manager-v1: efficient 1×1 solid-color buffers.
	 * GTK4 uses this for solid-color backgrounds — without it GTK4 apps fall
	 * back to a slower shm buffer path. */
	struct wlr_single_pixel_buffer_manager_v1 *single_pixel_buffer_manager;

	/* zwlr-data-control-v1: programmatic clipboard read/write.
	 * Used by wl-clipboard (wl-copy/wl-paste), clipboard managers, and
	 * screenshot tools that need to place data on the clipboard. */
	struct wlr_data_control_manager_v1 *data_control_manager;

	/* xdg-decoration-manager-v1: server-side vs client-side decoration
	 * negotiation. Lets the compositor tell clients whether to draw their
	 * own titlebars (CSD) or rely on the compositor to draw them (SSD).
	 * Without this every client assumes CSD and SSD is impossible. */
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
	struct wl_listener new_xdg_decoration;

	/* wp-tearing-control-v1: lets clients opt into tearing for lower
	 * latency. Games and real-time apps request this to skip vsync sync
	 * when they miss the frame deadline — same as "allow tearing" in GPUs. */
	struct wlr_tearing_control_manager_v1 *tearing_control_manager;
	struct wl_listener new_tearing_control;

    // Config Sync
    struct tinywl_config config;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	/* Ordered scene trees — rendered bottom to top:
	 *   layer_bg       ZWLR BACKGROUND layer
	 *   layer_bottom   ZWLR BOTTOM layer
	 *   layer_windows  all xdg toplevels — sandwiched between shell layers
	 *   layer_top      ZWLR TOP layer  (taskbar, dock, lumeoverlay, etc.)
	 *   layer_overlay  ZWLR OVERLAY layer (OSK, lock screen)
	 *
	 * Windows can never visually enter TOP or OVERLAY regardless of any
	 * raise-to-top call, because those trees are always rendered after the
	 * window tree. */
	struct wlr_scene_tree *layer_bg;
	struct wlr_scene_tree *layer_bottom;
	struct wlr_scene_tree *layer_windows;
	struct wlr_scene_tree *layer_top;
	struct wlr_scene_tree *layer_overlay;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener pointer_focus_change;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
	enum tinywl_cursor_mode cursor_mode;
	struct tinywl_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;

	/* Tracked layer surfaces — used to recompute usable_area on commit/destroy */
	struct wl_list layer_surfaces;

	/* The region windows are allowed to occupy, shrunk by exclusive zones.
	 * Updated every time a layer surface commits or is destroyed. */
	struct wlr_box usable_area;

	bool should_quit;
};

struct tinywl_output {
	struct wl_list link;
	struct tinywl_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

struct tinywl_toplevel {
	struct wl_list link;
	struct tinywl_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	/* wlr-foreign-toplevel handle — exposes this window to taskbars */
	struct wlr_foreign_toplevel_handle_v1 *foreign_toplevel;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
};

struct tinywl_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct tinywl_keyboard {
	struct wl_list link;
	struct tinywl_server *server;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};







struct tinywl_layer_surface {
	struct wl_list link;
	struct tinywl_server *server;
	struct wlr_layer_surface_v1 *wlr_layer_surface;
	struct wlr_scene_layer_surface_v1 *scene_layer_surface;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener surface_commit;
};

/* Tracks a single active pointer constraint (lock or confine) so we can
 * activate/deactivate it when pointer focus changes. */
struct tinywl_pointer_constraint {
	struct wlr_pointer_constraint_v1 *constraint;
	struct tinywl_server *server;
	struct wl_listener destroy;
};

/* ── output-management handlers ─────────────────────────────────────────── */

static void output_manager_apply(struct wl_listener *listener, void *data) {
	struct tinywl_server *server =
		wl_container_of(listener, server, output_manager_apply);
	struct wlr_output_configuration_v1 *config = data;

	bool ok = true;
	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &config->heads, link) {
		struct wlr_output *output = head->state.output;
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, head->state.enabled);
		if (head->state.enabled) {
			if (head->state.mode) {
				wlr_output_state_set_mode(&state, head->state.mode);
			}
			wlr_output_state_set_scale(&state, head->state.scale);
			wlr_output_state_set_transform(&state, head->state.transform);
			wlr_output_layout_add(server->output_layout, output,
				head->state.x, head->state.y);
		}
		if (!wlr_output_commit_state(output, &state)) {
			ok = false;
		}
		wlr_output_state_finish(&state);
	}

	if (ok) {
		wlr_output_configuration_v1_send_succeeded(config);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);
}

static void output_manager_test(struct wl_listener *listener, void *data) {
	struct tinywl_server *server =
		wl_container_of(listener, server, output_manager_test);
	struct wlr_output_configuration_v1 *config = data;

	/* Test only — don't commit, just validate. */
	bool ok = true;
	struct wlr_output_configuration_head_v1 *head;
	wl_list_for_each(head, &config->heads, link) {
		struct wlr_output *output = head->state.output;
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, head->state.enabled);
		if (head->state.enabled && head->state.mode) {
			wlr_output_state_set_mode(&state, head->state.mode);
		}
		if (!wlr_output_test_state(output, &state)) {
			ok = false;
		}
		wlr_output_state_finish(&state);
	}

	if (ok) {
		wlr_output_configuration_v1_send_succeeded(config);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);
}

/* ── output-power-management handler ────────────────────────────────────── */

static void output_power_set_mode(struct wl_listener *listener, void *data) {
	(void)listener;
	struct wlr_output_power_v1_set_mode_event *event = data;
	struct wlr_output_state state;
	wlr_output_state_init(&state);

	switch (event->mode) {
	case ZWLR_OUTPUT_POWER_V1_MODE_OFF:
		wlr_output_state_set_enabled(&state, false);
		break;
	case ZWLR_OUTPUT_POWER_V1_MODE_ON:
		wlr_output_state_set_enabled(&state, true);
		break;
	default:
		wlr_output_state_finish(&state);
		return;
	}

	wlr_output_commit_state(event->output, &state);
	wlr_output_state_finish(&state);
}

/* ── pointer-constraint handlers ─────────────────────────────────────────── */

static void pointer_constraint_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_pointer_constraint *pc =
		wl_container_of(listener, pc, destroy);
	wl_list_remove(&pc->destroy.link);
	free(pc);
}

static void server_new_pointer_constraint(struct wl_listener *listener, void *data) {
	struct tinywl_server *server =
		wl_container_of(listener, server, new_pointer_constraint);
	struct wlr_pointer_constraint_v1 *constraint = data;

	struct tinywl_pointer_constraint *pc = calloc(1, sizeof(*pc));
	pc->constraint = constraint;
	pc->server     = server;

	pc->destroy.notify = pointer_constraint_destroy;
	wl_signal_add(&constraint->events.destroy, &pc->destroy);

	/* If the constrained surface already has pointer focus, activate now. */
	struct wlr_surface *focused =
		server->seat->pointer_state.focused_surface;
	if (focused && focused == constraint->surface) {
		wlr_pointer_constraint_v1_send_activated(constraint);
	}
}

/* ── cursor-shape handler ─────────────────────────────────────────────────── */

static void seat_request_set_shape(struct wl_listener *listener, void *data) {
	struct tinywl_server *server =
		wl_container_of(listener, server, request_set_shape);
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;

	/* Only honour requests from the currently focused client. */
	struct wlr_seat_client *focused =
		server->seat->pointer_state.focused_client;
	if (event->seat_client != focused) return;

	const char *shape_name =
		wlr_cursor_shape_v1_name(event->shape);
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, shape_name);
}

/* ── idle-inhibit handler ─────────────────────────────────────────────────── */

static void new_idle_inhibitor(struct wl_listener *listener, void *data) {
	/* wlroots tracks inhibitors internally and exposes a has_active_inhibitor
	 * helper. We don't need per-inhibitor state — just acknowledge creation.
	 * An idle daemon queries wlr_idle_inhibit_manager_v1 directly. */
	(void)listener;
	(void)data;
}

/* ── virtual-keyboard handler ─────────────────────────────────────────────── */

static void server_new_virtual_keyboard(struct wl_listener *listener, void *data) {
	struct tinywl_server *server =
		wl_container_of(listener, server, new_virtual_keyboard);
	struct wlr_virtual_keyboard_v1 *vkb = data;

	/* Attach the virtual keyboard to our seat so it generates normal key
	 * events processed by the existing keyboard handler path. */
	wlr_seat_set_keyboard(server->seat, &vkb->keyboard);
}

/* ── virtual-pointer handler ──────────────────────────────────────────────── */

static void server_new_virtual_pointer(struct wl_listener *listener, void *data) {
	struct tinywl_server *server =
		wl_container_of(listener, server, new_virtual_pointer);
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_virtual_pointer_v1 *vptr = event->new_pointer;

	wlr_cursor_attach_input_device(server->cursor, &vptr->pointer.base);

	if (event->suggested_output) {
		wlr_cursor_map_input_to_output(server->cursor,
			&vptr->pointer.base, event->suggested_output);
	}
}

/* ── xdg-decoration handler ───────────────────────────────────────────────── */

/* Per-decoration state — owns the listeners so they can be cleaned up. */
struct tinywl_xdg_decoration {
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener request_mode;
	struct wl_listener surface_commit;
	struct wl_listener destroy;
};

static void xdg_decoration_handle_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_xdg_decoration *d = wl_container_of(listener, d, destroy);
	wl_list_remove(&d->request_mode.link);
	wl_list_remove(&d->surface_commit.link);
	wl_list_remove(&d->destroy.link);
	free(d);
}

static void xdg_decoration_handle_surface_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_xdg_decoration *d = wl_container_of(listener, d, surface_commit);
	if (!d->decoration->toplevel->base->initial_commit) return;
	/* First commit — surface is now initialized, safe to negotiate mode.
	 * Prefer SSD unless the client explicitly requested CSD. */
	enum wlr_xdg_toplevel_decoration_v1_mode requested =
		d->decoration->requested_mode;
	enum wlr_xdg_toplevel_decoration_v1_mode mode =
		(requested == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE)
		? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
		: WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
	wlr_xdg_toplevel_decoration_v1_set_mode(d->decoration, mode);
}

static void xdg_decoration_handle_request_mode(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_xdg_decoration *d = wl_container_of(listener, d, request_mode);
	if (!d->decoration->toplevel->base->initialized) return;
	/* Honour the client's requested mode. If they want CSD (own titlebar),
	 * grant it — otherwise default to SSD so we can move them. */
	enum wlr_xdg_toplevel_decoration_v1_mode requested =
		d->decoration->requested_mode;
	enum wlr_xdg_toplevel_decoration_v1_mode mode =
		(requested == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE)
		? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
		: WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
	wlr_xdg_toplevel_decoration_v1_set_mode(d->decoration, mode);
}

static void server_new_xdg_decoration(struct wl_listener *listener, void *data) {
	(void)listener;
	struct wlr_xdg_toplevel_decoration_v1 *decoration = data;

	struct tinywl_xdg_decoration *d = calloc(1, sizeof(*d));
	d->decoration = decoration;

	d->request_mode.notify = xdg_decoration_handle_request_mode;
	wl_signal_add(&decoration->events.request_mode, &d->request_mode);

	/* Listen on the underlying surface commit so we can send SSD after
	 * initial_commit, which is the earliest point set_mode is safe to call. */
	d->surface_commit.notify = xdg_decoration_handle_surface_commit;
	wl_signal_add(&decoration->toplevel->base->surface->events.commit,
		&d->surface_commit);

	d->destroy.notify = xdg_decoration_handle_destroy;
	wl_signal_add(&decoration->events.destroy, &d->destroy);
}

/* ── tearing-control handler ──────────────────────────────────────────────── */

static void server_new_tearing_control(struct wl_listener *listener, void *data) {
	/* wlroots stores the tearing hint on the surface itself.
	 * The scene renderer reads surface->current.tearing_hint per frame. */
	(void)listener;
	(void)data;
}

static void layer_surface_map(struct wl_listener *listener, void *data) {
	struct tinywl_layer_surface *layer_surf = wl_container_of(listener, layer_surf, map);
	if (layer_surf->scene_layer_surface) {
		wlr_scene_node_set_enabled(&layer_surf->scene_layer_surface->tree->node, true);
	}
	(void)data;
}

static void layer_surface_unmap(struct wl_listener *listener, void *data) {
	struct tinywl_layer_surface *layer_surf = wl_container_of(listener, layer_surf, unmap);
	if (layer_surf->scene_layer_surface) {
		wlr_scene_node_set_enabled(&layer_surf->scene_layer_surface->tree->node, false);
	}
	(void)data;
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
	struct tinywl_layer_surface *layer_surf = wl_container_of(listener, layer_surf, destroy);
	struct tinywl_server *server = layer_surf->server;
	wl_list_remove(&layer_surf->map.link);
	wl_list_remove(&layer_surf->unmap.link);
	wl_list_remove(&layer_surf->destroy.link);
	wl_list_remove(&layer_surf->surface_commit.link);
	wl_list_remove(&layer_surf->link);
	free(layer_surf);
	server_update_usable_area(server);
	(void)data;
}

static void layer_surface_commit(struct wl_listener *listener, void *data) {
	struct tinywl_layer_surface *layer_surf = wl_container_of(listener, layer_surf, surface_commit);
	struct wlr_layer_surface_v1 *wlr_surf = layer_surf->wlr_layer_surface;
	struct tinywl_server *server = layer_surf->server;
	(void)data;

	struct wlr_output *output = wlr_output_layout_output_at(server->output_layout, 0, 0);
	if (!output) return;

	int width, height;
	wlr_output_effective_resolution(output, &width, &height);

	if (wlr_surf->initial_commit) {
		wlr_layer_surface_v1_configure(wlr_surf, wlr_surf->pending.desired_width, wlr_surf->pending.desired_height);
		return;
	}

	uint32_t anchor = wlr_surf->current.anchor;
	int x = 0, y = 0;

	if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) && !(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
		y = height - wlr_surf->current.desired_height - wlr_surf->current.margin.bottom;
	} else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) {
		y = wlr_surf->current.margin.top;
	}

	if ((anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) && !(anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
		x = width - wlr_surf->current.desired_width - wlr_surf->current.margin.right;
	} else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) {
		x = wlr_surf->current.margin.left;
	}

	if (layer_surf->scene_layer_surface) {
		wlr_scene_node_set_position(&layer_surf->scene_layer_surface->tree->node, x, y);
	if (wlr_surf->current.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
		struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
		if (kb) wlr_seat_keyboard_notify_enter(server->seat, wlr_surf->surface, kb->keycodes, kb->num_keycodes, &kb->modifiers);
	}
	}

	server_update_usable_area(server);
}

/* Recompute the usable area for windows by walking all mapped layer surfaces
 * and subtracting their exclusive zones from the full output rectangle.
 * Only surfaces with a positive exclusive_zone and a definite anchor edge
 * contribute — decorative surfaces (exclusive_zone == 0) are ignored. */
static void server_update_usable_area(struct tinywl_server *server) {
	struct wlr_output *output = wlr_output_layout_output_at(
		server->output_layout, 0, 0);
	if (!output) return;

	int ow, oh;
	wlr_output_effective_resolution(output, &ow, &oh);

	/* Accumulate insets per edge. Multiple panels on the same edge stack. */
	int inset_top = 0, inset_bottom = 0, inset_left = 0, inset_right = 0;

	struct tinywl_layer_surface *ls;
	wl_list_for_each(ls, &server->layer_surfaces, link) {
		struct wlr_layer_surface_v1 *wls = ls->wlr_layer_surface;
		int32_t ez = wls->current.exclusive_zone;
		/* ez == 0  -> decorative, no reservation
		 * ez == -1 -> surface opts out of exclusive zone logic entirely */
		if (ez <= 0) continue;

		uint32_t anchor = wls->current.anchor;
		bool at = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)    != 0;
		bool ab = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) != 0;
		bool al = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)   != 0;
		bool ar = (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)  != 0;

		/* A surface exclusively owns one edge — the one it is anchored to
		 * without also being anchored to the opposite edge.
		 * exclusive_zone already encodes the full reserved thickness;
		 * do NOT add margin on top of it. */
		if (at && !ab) inset_top    += ez;
		if (ab && !at) inset_bottom += ez;
		if (al && !ar) inset_left   += ez;
		if (ar && !al) inset_right  += ez;
	}

	server->usable_area.x      = inset_left;
	server->usable_area.y      = inset_top;
	server->usable_area.width  = ow - inset_left - inset_right;
	server->usable_area.height = oh - inset_top  - inset_bottom;

	if (server->usable_area.width  < 1) server->usable_area.width  = 1;
	if (server->usable_area.height < 1) server->usable_area.height = 1;
}

static void server_new_layer_surface(struct wl_listener *listener, void *data) {
	struct tinywl_server *server = wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *wlr_layer_surface = data;

	struct tinywl_layer_surface *layer_surf = calloc(1, sizeof(struct tinywl_layer_surface));
	layer_surf->server = server;
	layer_surf->wlr_layer_surface = wlr_layer_surface;

	/* Track in server list so usable_area can be recomputed. */
	wl_list_insert(&server->layer_surfaces, &layer_surf->link);

	/* Route the surface to the scene tree matching its layer enum.
	 * ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND=0, BOTTOM=1, TOP=2, OVERLAY=3.
	 * layer_windows sits between BOTTOM and TOP, so xdg toplevels can never
	 * visually enter TOP or OVERLAY regardless of raise calls. */
	struct wlr_scene_tree *target_tree;
	switch (wlr_layer_surface->pending.layer) {
	case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND: target_tree = server->layer_bg;      break;
	case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:     target_tree = server->layer_bottom;   break;
	case ZWLR_LAYER_SHELL_V1_LAYER_TOP:        target_tree = server->layer_top;      break;
	case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:    target_tree = server->layer_overlay;  break;
	default:                                   target_tree = server->layer_top;      break;
	}
	layer_surf->scene_layer_surface = wlr_scene_layer_surface_v1_create(
		target_tree, wlr_layer_surface);

	layer_surf->map.notify = layer_surface_map;
	wl_signal_add(&wlr_layer_surface->surface->events.map, &layer_surf->map);
	layer_surf->unmap.notify = layer_surface_unmap;
	wl_signal_add(&wlr_layer_surface->surface->events.unmap, &layer_surf->unmap);
	layer_surf->destroy.notify = layer_surface_destroy;
	wl_signal_add(&wlr_layer_surface->events.destroy, &layer_surf->destroy);
	layer_surf->surface_commit.notify = layer_surface_commit;
	wl_signal_add(&wlr_layer_surface->surface->events.commit, &layer_surf->surface_commit);
}

static void load_lumecomp_config(struct tinywl_server *server) {
    // Set default fallback variables matching your conf file
    server->config.gaps_in = 6;
    server->config.gaps_out = 8;
    server->config.border_width = 2;

    FILE *fp = fopen("lumecomp.conf", "r");
    if (!fp) {
        wlr_log(WLR_INFO, "Could not open lumecomp.conf, utilizing fallback variables.");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        // Strip trailing newlines
        line[strcspn(line, "\r\n")] = 0;
        
        if (line[0] == '#' || line[0] == '[' || line[0] == '\0') continue;

        char *key = strtok(line, "=");
        char *val = strtok(NULL, "=");
        if (!key || !val) continue;

        // Trim spaces
        while(*key == ' ') key++;
        while(*val == ' ') val++;
        char *end = key + strlen(key) - 1;
        while(end > key && *end == ' ') end--;
	*(end+1) = 0;

        // Sync with layout metrics
        if (strcmp(key, "gaps_in") == 0) server->config.gaps_in = atoi(val);
        if (strcmp(key, "gaps_out") == 0) server->config.gaps_out = atoi(val);
        if (strcmp(key, "border_width") == 0) server->config.border_width = atoi(val);
    }
    fclose(fp);
}

static void focus_toplevel(struct tinywl_toplevel *toplevel) {
	/* Note: this function only deals with keyboard focus. */
	if (toplevel == NULL) {
		return;
	}
	struct tinywl_server *server = toplevel->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
	if (prev_surface == surface) {
		/* Don't re-focus an already focused surface. */
		return;
	}
	if (prev_surface) {
		struct wlr_xdg_toplevel *prev_toplevel =
			wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_toplevel != NULL) {
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
			/* Tell taskbars the previously focused window lost focus. */
			struct tinywl_toplevel *prev_tl =
				prev_toplevel->base->data
				? ((struct wlr_scene_tree *)prev_toplevel->base->data)->node.data
				: NULL;
			if (prev_tl && prev_tl->foreign_toplevel) {
				wlr_foreign_toplevel_handle_v1_set_activated(
					prev_tl->foreign_toplevel, false);
			}
		}
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	/* Move the toplevel to the front */
	wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
	if (!wl_list_empty(&toplevel->link)) {
		wl_list_remove(&toplevel->link);
	}
	wl_list_insert(&server->toplevels, &toplevel->link);
	/* Activate the new surface */
	wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
	/* Tell taskbars this window is now the active one. */
	if (toplevel->foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_set_activated(
			toplevel->foreign_toplevel, true);
	}
	/*
	 * Tell the seat to have the keyboard enter this surface. wlroots will keep
	 * track of this and automatically send key events to the appropriate
	 * clients without additional work on your part.
	 */
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

static void keyboard_handle_modifiers(
		struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	/*
	 * A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same seat. You can swap out the underlying wlr_keyboard like this and
	 * wlr_seat handles this transparently.
	 */
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
}

static bool handle_keybinding(struct tinywl_server *server, xkb_keysym_t sym) {
    switch (sym) {
    case XKB_KEY_Escape:
        server->should_quit = true;
        return true;
    case XKB_KEY_t:
    case XKB_KEY_Return:
        // Matches: Super+T and Super+Return to run foot
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", "foot", (void *)NULL);
        }
        return true;
    case XKB_KEY_q:
        // Matches: Super+Q to close focused window
        if (!wl_list_empty(&server->toplevels)) {
            struct tinywl_toplevel *toplevel = wl_container_of(server->toplevels.next, toplevel, link);
            wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
        }
        return true;
    }
    return false;
}

static void keyboard_handle_key(
		struct wl_listener *listener, void *data) {
	/* This event is raised when a key is pressed or released. */
	struct tinywl_keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	struct tinywl_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if ((modifiers & WLR_MODIFIER_LOGO) &&
			event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		/* If alt is held down and this button was _pressed_, we attempt to
		 * process it as a compositor keybinding. */
		for (int i = 0; i < nsyms; i++) {
			handled = handle_keybinding(server, syms[i]);
		}
	}

	if (!handled) {
		/* Otherwise, we pass it along to the client. */
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct tinywl_server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct tinywl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	/* We need to prepare an XKB keymap and assign it to the keyboard. This
	 * assumes the defaults (e.g. layout = "us"). */
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	/* Here we set up listeners for keyboard events. */
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	/* And add the keyboard to our list of keyboards */
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct tinywl_server *server,
		struct wlr_input_device *device) {
	/* We don't do anything special with pointers. All of our pointer handling
	 * is proxied through wlr_cursor. On another compositor, you might take this
	 * opportunity to do libinput configuration on the device to set
	 * acceleration, etc. */
	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct tinywl_server *server =
		wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
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
	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In TinyWL we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct tinywl_server *server = wl_container_of(
			listener, server, request_cursor);
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;
	/* This can be sent by any client, so we check to make sure this one is
	 * actually has pointer focus first. */
	if (focused_client == event->seat_client) {
		/* Once we've vetted the client, we can tell the cursor to use the
		 * provided surface as the cursor image. It will set the hardware cursor
		 * on the output that it's currently on and continue to do so as the
		 * cursor moves between outputs. */
		wlr_cursor_set_surface(server->cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
	}
}

static void seat_pointer_focus_change(struct wl_listener *listener, void *data) {
	struct tinywl_server *server = wl_container_of(
		listener, server, pointer_focus_change);
	struct wlr_seat_pointer_focus_change_event *event = data;

	/* Reset cursor to default when focus leaves a surface. */
	if (event->new_surface == NULL) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}

	/* Deactivate any constraint on the old surface, activate on the new one. */
	if (server->pointer_constraints) {
		if (event->old_surface) {
			struct wlr_pointer_constraint_v1 *old_constraint =
				wlr_pointer_constraints_v1_constraint_for_surface(
					server->pointer_constraints,
					event->old_surface,
					server->seat);
			if (old_constraint) {
				wlr_pointer_constraint_v1_send_deactivated(old_constraint);
			}
		}
		if (event->new_surface) {
			struct wlr_pointer_constraint_v1 *new_constraint =
				wlr_pointer_constraints_v1_constraint_for_surface(
					server->pointer_constraints,
					event->new_surface,
					server->seat);
			if (new_constraint) {
				wlr_pointer_constraint_v1_send_activated(new_constraint);
			}
		}
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in tinywl we always honor
	 */
	struct tinywl_server *server = wl_container_of(
			listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static struct tinywl_toplevel *desktop_toplevel_at(
		struct tinywl_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	/* This returns the topmost node in the scene at the given layout coords.
	 * We only care about surface nodes as we are specifically looking for a
	 * surface in the surface tree of a tinywl_toplevel. */
	struct wlr_scene_node *node = wlr_scene_node_at(
		&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return NULL;
	}

	*surface = scene_surface->surface;
	/* Find the node corresponding to the tinywl_toplevel at the root of this
	 * surface tree, it is the only one for which we set the data field. */
	struct wlr_scene_tree *tree = node->parent;
	while (tree != NULL && tree->node.data == NULL) {
		tree = tree->node.parent;
	}
	return tree ? tree->node.data : NULL;
}

static void reset_cursor_mode(struct tinywl_server *server) {
	/* Reset the cursor mode to passthrough. */
	server->cursor_mode = TINYWL_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

static void process_cursor_move(struct tinywl_server *server) {
	struct tinywl_toplevel *toplevel = server->grabbed_toplevel;
	if (!toplevel) { reset_cursor_mode(server); return; }
	struct wlr_box *ua = &server->usable_area;

	int new_x = (int)(server->cursor->x - server->grab_x);
	int new_y = (int)(server->cursor->y - server->grab_y);

	/* Get the actual committed window geometry so we can clamp all 4 edges.
	 * geo_box is relative to the surface origin; width/height are the visible
	 * window size including decorations reported by the client. */
	struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
	int win_w = geo->width  > 0 ? geo->width  : 1;
	int win_h = geo->height > 0 ? geo->height : 1;

	/* Clamp so no part of the window overlaps an exclusive zone.
	 *
	 *  Left edge:   window left  (new_x) >= usable left
	 *  Top edge:    window top   (new_y) >= usable top
	 *  Right edge:  window right (new_x + win_w) <= usable right
	 *  Bottom edge: window bottom(new_y + win_h) <= usable bottom
	 *
	 * If a window is wider/taller than the usable area (unlikely but possible)
	 * we bias toward keeping the top-left visible so the title bar stays
	 * reachable. */
	int ua_right  = ua->x + ua->width;
	int ua_bottom = ua->y + ua->height;

	if (new_x + win_w > ua_right)  new_x = ua_right  - win_w;
	if (new_y + win_h > ua_bottom) new_y = ua_bottom - win_h;
	if (new_x < ua->x) new_x = ua->x;
	if (new_y < ua->y) new_y = ua->y;

	wlr_scene_node_set_position(&toplevel->scene_tree->node, new_x, new_y);
}

static void process_cursor_resize(struct tinywl_server *server) {
	struct tinywl_toplevel *toplevel = server->grabbed_toplevel;
	if (!toplevel) { reset_cursor_mode(server); return; }
	struct wlr_box *ua = &server->usable_area;

	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int new_left   = server->grab_geobox.x;
	int new_right  = server->grab_geobox.x + server->grab_geobox.width;
	int new_top    = server->grab_geobox.y;
	int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

	int ua_right  = ua->x + ua->width;
	int ua_bottom = ua->y + ua->height;

	if (server->resize_edges & WLR_EDGE_TOP) {
		new_top = (int)border_y;
		/* Don't let the top edge cross into the usable area boundary. */
		if (new_top < ua->y)    new_top = ua->y;
		if (new_top >= new_bottom) new_top = new_bottom - 1;
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		new_bottom = (int)border_y;
		if (new_bottom > ua_bottom) new_bottom = ua_bottom;
		if (new_bottom <= new_top)  new_bottom = new_top + 1;
	}

	if (server->resize_edges & WLR_EDGE_LEFT) {
		new_left = (int)border_x;
		if (new_left < ua->x)   new_left = ua->x;
		if (new_left >= new_right) new_left = new_right - 1;
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		new_right = (int)border_x;
		if (new_right > ua_right)  new_right = ua_right;
		if (new_right <= new_left) new_right = new_left + 1;
	}

	struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;
	wlr_scene_node_set_position(&toplevel->scene_tree->node,
		new_left - geo_box->x, new_top - geo_box->y);

	int new_width  = new_right  - new_left;
	int new_height = new_bottom - new_top;
	wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, new_width, new_height);
}

static void process_cursor_motion(struct tinywl_server *server, uint32_t time) {
	/* If the mode is non-passthrough, delegate to those functions. */
	if (server->cursor_mode == TINYWL_CURSOR_MOVE) {
		process_cursor_move(server);
		return;
	} else if (server->cursor_mode == TINYWL_CURSOR_RESIZE) {
		process_cursor_resize(server);
		return;
	}

	/* Otherwise, find the toplevel under the pointer and send the event along. */
	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct tinywl_toplevel *toplevel = desktop_toplevel_at(server,
			server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (!toplevel) {
		/* If there's no toplevel under the cursor, set the cursor image to a
		 * default. This is what makes the cursor image appear when you move it
		 * around the screen, not over any toplevels. */
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (surface) {
		/*
		 * Send pointer enter and motion events.
		 *
		 * The enter event gives the surface "pointer focus", which is distinct
		 * from keyboard focus. You get pointer focus by moving the pointer over
		 * a window.
		 *
		 * Note that wlroots will avoid sending duplicate enter/motion events if
		 * the surface has already has pointer focus or if the client is already
		 * aware of the coordinates passed.
		 */
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	} else {
		/* Clear pointer focus so future button events and such are not sent to
		 * the last client to have the cursor over it. */
		wlr_seat_pointer_clear_focus(seat);
	}
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct tinywl_server *server =
		wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	wlr_cursor_move(server->cursor, &event->pointer->base,
			event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(
		struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. There is also some hardware which
	 * emits these events. */
	struct tinywl_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
		event->y);
	process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits a button
	 * event. */
	struct tinywl_server *server =
		wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	/* Notify the client with pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(server->seat,
			event->time_msec, event->button, event->state);
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		/* If you released any buttons, we exit interactive move/resize mode. */
		reset_cursor_mode(server);
	} else {
		/* Focus that client if the button was _pressed_ */
		double sx, sy;
		struct wlr_surface *surface = NULL;
		struct tinywl_toplevel *toplevel = desktop_toplevel_at(server,
				server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		focus_toplevel(toplevel);
	}
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct tinywl_server *server =
		wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(server->seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_server *server =
		wl_container_of(listener, server, cursor_frame);
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(server->seat);
}

static void output_frame(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene *scene = output->server->scene;

	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
		scene, output->wlr_output);
	if (!scene_output) return;

	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
	/* This function is called when the backend requests a new state for
	 * the output. For example, Wayland and X11 backends request a new mode
	 * when the output window is resized. */
	struct tinywl_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_output *output = wl_container_of(listener, output, destroy);

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

static void server_new_output(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct tinywl_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	/* Configures the output created by the backend to use our allocator
	 * and our renderer. Must be done once, before committing the output */
	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	/* The output may be disabled, switch it on. */
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
		wlr_output_state_set_render_format(&state, DRM_FORMAT_ARGB8888);

	/* Some backends don't have modes. DRM+KMS does, and we need to set a mode
	 * before we can use the output. The mode is a tuple of (width, height,
	 * refresh rate), and each monitor supports only a specific set of modes. We
	 * just pick the monitor's preferred mode, a more sophisticated compositor
	 * would let the user configure it. */
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}

	/* Atomically applies the new output state. */
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	/* Allocates and configures our state for this output */
	struct tinywl_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;

	/* Sets up a listener for the frame event. */
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	/* Sets up a listener for the state request event. */
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);

	/* Sets up a listener for the destroy event. */
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	wl_list_insert(&server->outputs, &output->link);

	/* Adds this to the output layout. The add_auto function arranges outputs
	 * from left-to-right in the order they appear. A more sophisticated
	 * compositor would let the user configure the arrangement of outputs in the
	 * layout.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->output_layout,
		wlr_output);
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, map);

	if (toplevel->foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_set_minimized(toplevel->foreign_toplevel, false);
		/* Output association so taskbars know which monitor this window is on. */
		struct wlr_output *output = wlr_output_layout_output_at(
			toplevel->server->output_layout, 0, 0);
		if (output) {
			wlr_foreign_toplevel_handle_v1_output_enter(
				toplevel->foreign_toplevel, output);
		}
	}

	focus_toplevel(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
	struct tinywl_server *server = toplevel->server;
	if (toplevel == server->grabbed_toplevel) {
		server->cursor_mode = TINYWL_CURSOR_PASSTHROUGH;
		server->grabbed_toplevel = NULL;
	}
	struct wlr_surface *focused_surface = server->seat->pointer_state.focused_surface;
	if (focused_surface && wlr_xdg_surface_try_from_wlr_surface(focused_surface) == toplevel->xdg_toplevel->base) {
		wlr_seat_pointer_clear_focus(server->seat);
	}
	if (toplevel->link.next && toplevel->link.prev) {
		wl_list_remove(&toplevel->link);
		wl_list_init(&toplevel->link);
	}
	/* Tell taskbars this window is now hidden/minimized. */
	if (toplevel->foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_set_minimized(toplevel->foreign_toplevel, true);
	}
	(void)data;
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

	if (toplevel->xdg_toplevel->base->initial_commit) {
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
	}

	/* Keep the foreign toplevel handle in sync with title/app_id/state. */
	if (toplevel->foreign_toplevel) {
		const char *title  = toplevel->xdg_toplevel->title  ? toplevel->xdg_toplevel->title  : "";
		const char *app_id = toplevel->xdg_toplevel->app_id ? toplevel->xdg_toplevel->app_id : "";
		wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign_toplevel, title);
		wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign_toplevel, app_id);
		wlr_foreign_toplevel_handle_v1_set_maximized(toplevel->foreign_toplevel,
			toplevel->xdg_toplevel->current.maximized);
		wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel->foreign_toplevel,
			toplevel->xdg_toplevel->current.fullscreen);
	}
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	/* Cancel any in-progress grab on this window. */
	if (toplevel->server->grabbed_toplevel == toplevel) {
		toplevel->server->cursor_mode = TINYWL_CURSOR_PASSTHROUGH;
		toplevel->server->grabbed_toplevel = NULL;
	}

	/* Remove all listeners. commit must be removed to prevent use-after-free. */
	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);

	/* Remove from server toplevel list if still linked. */
	if (toplevel->link.next != NULL) {
		wl_list_remove(&toplevel->link);
	}

	/* Notify taskbars that this window is gone. */
	if (toplevel->foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_destroy(toplevel->foreign_toplevel);
		toplevel->foreign_toplevel = NULL;
	}

	free(toplevel);
}

static void begin_interactive(struct tinywl_toplevel *toplevel,
		enum tinywl_cursor_mode mode, uint32_t edges) {
	/* This function sets up an interactive move or resize operation, where the
	 * compositor stops propagating pointer events to clients and instead
	 * consumes them itself, to move or resize windows. */
	struct tinywl_server *server = toplevel->server;

	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;

	if (mode == TINYWL_CURSOR_MOVE) {
		server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
		server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
	} else {
		struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;

		double border_x = (toplevel->scene_tree->node.x + geo_box->x) +
			((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
		double border_y = (toplevel->scene_tree->node.y + geo_box->y) +
			((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;

		server->grab_geobox = *geo_box;
		server->grab_geobox.x += toplevel->scene_tree->node.x;
		server->grab_geobox.y += toplevel->scene_tree->node.y;

		server->resize_edges = edges;
	}
}

static void xdg_toplevel_request_move(
		struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	begin_interactive(toplevel, TINYWL_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(
		struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	begin_interactive(toplevel, TINYWL_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(
		struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_maximize);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

static void xdg_toplevel_request_fullscreen(
		struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_fullscreen);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	/* This event is raised when a client creates a new toplevel (application window). */
	struct tinywl_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	/* Allocate a tinywl_toplevel for this surface */
	struct tinywl_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
	wl_list_init(&toplevel->link);
	toplevel->scene_tree =
		wlr_scene_xdg_surface_create(toplevel->server->layer_windows, xdg_toplevel->base);
	toplevel->scene_tree->node.data = toplevel;
	xdg_toplevel->base->data = toplevel->scene_tree;

	/* Create a foreign toplevel handle so taskbars (lume taskbar) can see
	 * this window, read its title/app_id, and send activate/close requests. */
	if (server->foreign_toplevel_manager) {
		toplevel->foreign_toplevel =
			wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevel_manager);
		const char *title   = xdg_toplevel->title   ? xdg_toplevel->title   : "";
		const char *app_id  = xdg_toplevel->app_id  ? xdg_toplevel->app_id  : "";
		wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign_toplevel, title);
		wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign_toplevel, app_id);
	}

	/* Listen to the various events it can emit */
	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

	/* cotd */
	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_popup *popup = wl_container_of(listener, popup, commit);
	if (popup->xdg_popup->base->initial_commit) {
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	(void)data;
	struct tinywl_popup *popup = wl_container_of(listener, popup, destroy);
	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	(void)listener;
	struct wlr_xdg_popup *xdg_popup = data;

	struct tinywl_popup *popup = calloc(1, sizeof(*popup));
	popup->xdg_popup = xdg_popup;

	/* We must add xdg popups to the scene graph so they get rendered. The
	 * wlroots scene graph provides a helper for this, but to use it we must
	 * provide the proper parent scene node of the xdg popup. To enable this,
	 * we always set the user data field of xdg_surfaces to the corresponding
	 * scene node. */
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	assert(parent != NULL);
	struct wlr_scene_tree *parent_tree = parent->data;
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);
	char *startup_cmd = NULL;

	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("Usage: %s [-s startup command]\n", argv[0]);
			return 0;
		}
	}
	if (optind < argc) {
		printf("Usage: %s [-s startup command]\n", argv[0]);
		return 0;
	}

	struct tinywl_server server = {0};

	/* Ignore SIGPIPE — when a client crashes its socket closes, and the next
	 * write() from the compositor would receive SIGPIPE whose default action
	 * is to kill the process. With SIG_IGN the write returns EPIPE instead
	 * and wlroots cleans up the dead client on the next event loop tick. */
	signal(SIGPIPE, SIG_IGN);

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, managing Wayland globals, and so on. */
	server.wl_display = wl_display_create();
	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. */
	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
	if (server.backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		return 1;
	}

	/* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
	 * can also specify a renderer using the WLR_RENDERER env var.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_renderer");
		return 1;
	}

	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the
	 * screen */
	server.allocator = wlr_allocator_autocreate(server.backend,
		server.renderer);
	if (server.allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		return 1;
	}

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces, the subcompositor allows to
	 * assign the role of subsurfaces to surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the handling of the request_set_selection event below.*/
	wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	/* Creates an output layout, which a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	server.output_layout = wlr_output_layout_create(server.wl_display);

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	wl_list_init(&server.layer_surfaces);
	/* usable_area is populated once layer surfaces commit; until then,
	 * default to the full output size — we don't know it yet at init time,
	 * so use large sentinel values and let it be corrected on first commit. */
	server.usable_area = (struct wlr_box){0, 0, 65535, 65535};

	/* Create a scene graph. This is a wlroots abstraction that handles all
	 * rendering and damage tracking. All the compositor author needs to do
	 * is add things that should be rendered to the scene graph at the proper
	 * positions and then call wlr_scene_output_commit() to render a frame if
	 * necessary.
	 */
	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

	/* Build the z-ordered layer stack. Trees are appended to the scene's
	 * child list in creation order — last created = topmost visually.
	 * Creating them in render order (bg → bottom → windows → top → overlay)
	 * gives correct paint order automatically; no manual raise/lower needed.
	 *
	 * The critical invariant: xdg toplevels always live in layer_windows.
	 * wlr_scene_node_raise_to_top() on a toplevel only moves it within that
	 * tree, so it can never visually exceed layer_top or layer_overlay. */
	server.layer_bg      = wlr_scene_tree_create(&server.scene->tree);
	server.layer_bottom  = wlr_scene_tree_create(&server.scene->tree);
	server.layer_windows = wlr_scene_tree_create(&server.scene->tree);
	server.layer_top     = wlr_scene_tree_create(&server.scene->tree);
	server.layer_overlay = wlr_scene_tree_create(&server.scene->tree);

	/* Set up xdg-shell version 3. The xdg-shell is a Wayland protocol which is
	 * used for application windows. For more detail on shells, refer to
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html.
	 */
	wl_list_init(&server.toplevels);
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
	server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). */
	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	/*
	 * wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around. More detail on this process is described in
	 * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html.
	 *
	 * And more comments are sprinkled throughout the notify functions above.
	 */
	server.cursor_mode = TINYWL_CURSOR_PASSTHROUGH;
	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute,
			&server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	wl_list_init(&server.keyboards);
	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor,
			&server.request_cursor);
	server.pointer_focus_change.notify = seat_pointer_focus_change;
	wl_signal_add(&server.seat->pointer_state.events.focus_change,
			&server.pointer_focus_change);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection,
			&server.request_set_selection);
	load_lumecomp_config(&server);
	server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
	server.new_layer_surface.notify = server_new_layer_surface;
	wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);
	server.screencopy_manager = wlr_screencopy_manager_v1_create(server.wl_display);

	/* xdg-output-v1: exposes logical output name, position, and scale to clients.
	 * Required by layer-shell clients to identify outputs in multi-monitor setups,
	 * and by most status bars, including lumeoverlay and the lume taskbar. */
	server.xdg_output_manager = wlr_xdg_output_manager_v1_create(
		server.wl_display, server.output_layout);

	/* wlr-output-management-v1: lets clients enumerate and reconfigure outputs
	 * (resolution, refresh rate, position, enabled state). The Lume settings
	 * app uses this for display configuration. */
	server.output_manager = wlr_output_manager_v1_create(server.wl_display);
	server.output_manager_apply.notify = output_manager_apply;
	wl_signal_add(&server.output_manager->events.apply, &server.output_manager_apply);
	server.output_manager_test.notify = output_manager_test;
	wl_signal_add(&server.output_manager->events.test, &server.output_manager_test);

	/* wlr-output-power-management-v1: DPMS / screen blanking.
	 * Allows the lock screen and idle daemon to turn outputs off and on. */
	server.output_power_manager = wlr_output_power_manager_v1_create(server.wl_display);
	server.output_power_set_mode.notify = output_power_set_mode;
	wl_signal_add(&server.output_power_manager->events.set_mode,
		&server.output_power_set_mode);

	/* zwp-pointer-constraints-v1: pointer lock and confinement regions.
	 * Required for games, 3D viewports, and any app that needs to capture
	 * or restrict the mouse without it leaving a surface. */
	server.pointer_constraints = wlr_pointer_constraints_v1_create(server.wl_display);
	server.new_pointer_constraint.notify = server_new_pointer_constraint;
	wl_signal_add(&server.pointer_constraints->events.new_constraint,
		&server.new_pointer_constraint);

	/* zwp-relative-pointer-manager-v1: delivers relative (delta) pointer motion
	 * events. Works together with pointer-constraints — games request both to
	 * get raw unconstrained deltas with the cursor locked on screen. */
	server.relative_pointer_manager =
		wlr_relative_pointer_manager_v1_create(server.wl_display);

	/* wp-cursor-shape-manager-v1: lets clients set a named cursor shape (e.g.
	 * "text", "crosshair", "grab") without having to provide a surface buffer.
	 * GTK4, Qt6, and Chromium all prefer this over wl_surface cursors. */
	server.cursor_shape_manager =
		wlr_cursor_shape_manager_v1_create(server.wl_display, 1);
	server.request_set_shape.notify = seat_request_set_shape;
	wl_signal_add(&server.cursor_shape_manager->events.request_set_shape,
		&server.request_set_shape);

	/* wp-fractional-scale-manager-v1: non-integer HiDPI scaling (e.g. 1.25x,
	 * 1.5x). Without this, only integer scales work and most HiDPI laptops
	 * render blurry. GTK4 and Qt6 both negotiate fractional scale via this. */
	server.fractional_scale_manager =
		wlr_fractional_scale_manager_v1_create(server.wl_display, 1);

	/* wp-viewporter: lets clients crop and scale their surface buffers before
	 * compositing. Required by video players and is a hard dependency for
	 * fractional scaling to work correctly end-to-end. */
	server.viewporter = wlr_viewporter_create(server.wl_display);

	/* wlr-foreign-toplevel-management-v1: exposes the live window list and
	 * per-window state (title, app_id, activated, maximized, minimized,
	 * fullscreen, output) to external clients. The lume taskbar subscribes
	 * to this to show window buttons and send activate/close requests. */
	server.foreign_toplevel_manager =
		wlr_foreign_toplevel_manager_v1_create(server.wl_display);

	/* zwp-idle-inhibit-manager-v1: lets apps (video players, presentations,
	 * games) create idle inhibitors that prevent the screen from blanking.
	 * An idle daemon checks wlr_idle_inhibit_manager_v1 before blanking. */
	server.idle_inhibit_manager =
		wlr_idle_inhibit_v1_create(server.wl_display);
	server.new_idle_inhibitor.notify = new_idle_inhibitor;
	wl_signal_add(&server.idle_inhibit_manager->events.new_inhibitor,
		&server.new_idle_inhibitor);

	/* wlr-virtual-keyboard-v1: synthetic keyboard input from non-hardware
	 * sources. On-screen keyboards (wvkbd) and accessibility tools use this
	 * to inject key events as if from a real keyboard. */
	server.virtual_keyboard_manager =
		wlr_virtual_keyboard_manager_v1_create(server.wl_display);
	server.new_virtual_keyboard.notify = server_new_virtual_keyboard;
	wl_signal_add(&server.virtual_keyboard_manager->events.new_virtual_keyboard,
		&server.new_virtual_keyboard);

	/* zwp-virtual-pointer-manager-v1: synthetic pointer input from non-hardware
	 * sources. Used by wl-mirror, screen recording tools, and accessibility
	 * software to inject cursor motion and button events. */
	server.virtual_pointer_manager =
		wlr_virtual_pointer_manager_v1_create(server.wl_display);
	server.new_virtual_pointer.notify = server_new_virtual_pointer;
	wl_signal_add(&server.virtual_pointer_manager->events.new_virtual_pointer,
		&server.new_virtual_pointer);

	/* wp-single-pixel-buffer-manager-v1: lets clients allocate an efficient
	 * 1×1 solid-color buffer without going through shm. GTK4 uses this for
	 * all solid-color surface fills — without it GTK4 apps are slower. */
	server.single_pixel_buffer_manager =
		wlr_single_pixel_buffer_manager_v1_create(server.wl_display);

	/* zwlr-data-control-v1: programmatic clipboard read/write.
	 * wl-clipboard (wl-copy / wl-paste), clipboard managers, and screenshot
	 * tools use this to read from and write to the Wayland clipboard without
	 * needing a focused surface. */
	server.data_control_manager =
		wlr_data_control_manager_v1_create(server.wl_display);

	/* xdg-decoration-manager-v1: SSD vs CSD negotiation.
	 * We advertise server-side decorations as preferred so Lume OS can draw
	 * a consistent titlebar. Clients that insist on CSD still work — they
	 * send set_mode(CLIENT_SIDE) and we honour it. Without this global every
	 * client unconditionally draws its own titlebar. */
	server.xdg_decoration_manager =
		wlr_xdg_decoration_manager_v1_create(server.wl_display);
	server.new_xdg_decoration.notify = server_new_xdg_decoration;
	wl_signal_add(&server.xdg_decoration_manager->events.new_toplevel_decoration,
		&server.new_xdg_decoration);

	/* wp-tearing-control-v1: per-surface tearing hint.
	 * Games and low-latency apps set this to ASYNC to opt out of vsync
	 * synchronisation when they miss a frame deadline. wlroots stores the
	 * hint on the surface and the scene renderer respects it per output. */
	server.tearing_control_manager =
		wlr_tearing_control_manager_v1_create(server.wl_display, 1);
	server.new_tearing_control.notify = server_new_tearing_control;
	wl_signal_add(&server.tearing_control_manager->events.new_object,
		&server.new_tearing_control);

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	/* Set the WAYLAND_DISPLAY environment variable to our socket and run the
	 * startup command if requested. */
	setenv("WAYLAND_DISPLAY", socket, true);
	if (startup_cmd) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
		}
	}
	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
			socket);

	/* Manual event loop instead of wl_display_run().
	 *
	 * wl_display_run() stops the compositor whenever wl_event_loop_dispatch()
	 * returns an error — which happens when a client fd goes dead (EPIPE,
	 * ECONNRESET). A crashing app kills the whole compositor session.
	 *
	 * This loop fixes that:
	 *   1. wl_display_flush_clients() is called first. libwayland internally
	 *      calls write() on each client fd; if it gets EPIPE it destroys that
	 *      client cleanly without touching the event loop.
	 *   2. wl_event_loop_dispatch() is then called. Any remaining fd errors
	 *      from already-dead clients have been cleared by step 1.
	 *   3. EINTR (signal delivery mid-dispatch) is explicitly ignored.
	 */
	struct wl_event_loop *loop = wl_display_get_event_loop(server.wl_display);
	while (!server.should_quit) {
		wl_display_flush_clients(server.wl_display);
		if (wl_event_loop_dispatch(loop, -1) < 0 && errno != EINTR)
			break;
	}


	/* Once wl_display_run returns, we destroy all clients then shut down the
	 * server. */
	wl_display_destroy_clients(server.wl_display);

	wl_list_remove(&server.new_xdg_toplevel.link);
	wl_list_remove(&server.new_xdg_popup.link);

	wl_list_remove(&server.cursor_motion.link);
	wl_list_remove(&server.cursor_motion_absolute.link);
	wl_list_remove(&server.cursor_button.link);
	wl_list_remove(&server.cursor_axis.link);
	wl_list_remove(&server.cursor_frame.link);

	wl_list_remove(&server.new_input.link);
	wl_list_remove(&server.request_cursor.link);
	wl_list_remove(&server.pointer_focus_change.link);
	wl_list_remove(&server.request_set_selection.link);

	wl_list_remove(&server.new_output.link);
	wl_list_remove(&server.output_manager_apply.link);
	wl_list_remove(&server.output_manager_test.link);
	wl_list_remove(&server.output_power_set_mode.link);
	wl_list_remove(&server.new_pointer_constraint.link);
	wl_list_remove(&server.request_set_shape.link);
	wl_list_remove(&server.new_idle_inhibitor.link);
	wl_list_remove(&server.new_virtual_keyboard.link);
	wl_list_remove(&server.new_virtual_pointer.link);
	wl_list_remove(&server.new_xdg_decoration.link);
	wl_list_remove(&server.new_tearing_control.link);

	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);
	return 0;
}
