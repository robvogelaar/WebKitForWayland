/* 
 */

#include <stdlib.h>
#include <stdint.h>
#include "wayland-util.h"

extern const struct wl_interface wl_surface_interface;

static const struct wl_interface *types[] = {
	&wl_surface_interface,
	NULL,
	NULL,
	&wl_surface_interface,
	NULL,
};

static const struct wl_message wl_bcmrpi_dispmanx_requests[] = {
	{ "create_element", "ouu", types + 0 },
};

static const struct wl_message wl_bcmrpi_dispmanx_events[] = {
	{ "element_created", "ou", types + 3 },
};

WL_EXPORT const struct wl_interface wl_bcmrpi_dispmanx_interface = {
	"wl_bcmrpi_dispmanx", 1,
	1, wl_bcmrpi_dispmanx_requests,
	1, wl_bcmrpi_dispmanx_events,
};

