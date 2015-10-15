/* 
 */

#ifndef BCMRPI_DISPMANX_CLIENT_PROTOCOL_H
#define BCMRPI_DISPMANX_CLIENT_PROTOCOL_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "wayland-client.h"

struct wl_client;
struct wl_resource;

struct wl_bcmrpi_dispmanx;

extern const struct wl_interface wl_bcmrpi_dispmanx_interface;

struct wl_bcmrpi_dispmanx_listener {
	/**
	 * element_created - (none)
	 * @surface: (none)
	 * @handle: (none)
	 */
	void (*element_created)(void *data,
				struct wl_bcmrpi_dispmanx *wl_bcmrpi_dispmanx,
				struct wl_surface *surface,
				uint32_t handle);
};

static inline int
wl_bcmrpi_dispmanx_add_listener(struct wl_bcmrpi_dispmanx *wl_bcmrpi_dispmanx,
				const struct wl_bcmrpi_dispmanx_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *) wl_bcmrpi_dispmanx,
				     (void (**)(void)) listener, data);
}

#define WL_BCMRPI_DISPMANX_CREATE_ELEMENT	0

static inline void
wl_bcmrpi_dispmanx_set_user_data(struct wl_bcmrpi_dispmanx *wl_bcmrpi_dispmanx, void *user_data)
{
	wl_proxy_set_user_data((struct wl_proxy *) wl_bcmrpi_dispmanx, user_data);
}

static inline void *
wl_bcmrpi_dispmanx_get_user_data(struct wl_bcmrpi_dispmanx *wl_bcmrpi_dispmanx)
{
	return wl_proxy_get_user_data((struct wl_proxy *) wl_bcmrpi_dispmanx);
}

static inline void
wl_bcmrpi_dispmanx_destroy(struct wl_bcmrpi_dispmanx *wl_bcmrpi_dispmanx)
{
	wl_proxy_destroy((struct wl_proxy *) wl_bcmrpi_dispmanx);
}

static inline void
wl_bcmrpi_dispmanx_create_element(struct wl_bcmrpi_dispmanx *wl_bcmrpi_dispmanx, struct wl_surface *surface, uint32_t width, uint32_t height)
{
	wl_proxy_marshal((struct wl_proxy *) wl_bcmrpi_dispmanx,
			 WL_BCMRPI_DISPMANX_CREATE_ELEMENT, surface, width, height);
}

#ifdef  __cplusplus
}
#endif

#endif
