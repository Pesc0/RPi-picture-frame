/*
 * Copyright (c) 2017 Rob Clark <rclark@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <poll.h>
#include <stdbool.h>

#include "drm.h"


struct drm_fb {
	struct gbm_bo *bo;
	uint32_t fb_id;
};


WEAK union gbm_bo_handle
gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane);

WEAK uint64_t
gbm_bo_get_modifier(struct gbm_bo *bo);

WEAK int
gbm_bo_get_plane_count(struct gbm_bo *bo);

WEAK uint32_t
gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane);

WEAK uint32_t
gbm_bo_get_offset(struct gbm_bo *bo, int plane);

static void
drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
	int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
	struct drm_fb *fb = data;

	if (fb->fb_id)
		drmModeRmFB(drm_fd, fb->fb_id);

	free(fb);
}

struct drm_fb * drm_fb_get_from_bo(struct gbm_bo *bo)
{
	int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
	struct drm_fb *fb = gbm_bo_get_user_data(bo);
	uint32_t width, height, format,
		 strides[4] = {0}, handles[4] = {0},
		 offsets[4] = {0}, flags = 0;
	int ret = -1;

	if (fb)
		return fb;

	fb = calloc(1, sizeof *fb);
	fb->bo = bo;

	width = gbm_bo_get_width(bo);
	height = gbm_bo_get_height(bo);
	format = gbm_bo_get_format(bo);

	if (gbm_bo_get_handle_for_plane && gbm_bo_get_modifier &&
	    gbm_bo_get_plane_count && gbm_bo_get_stride_for_plane &&
	    gbm_bo_get_offset) {

		uint64_t modifiers[4] = {0};
		modifiers[0] = gbm_bo_get_modifier(bo);
		const int num_planes = gbm_bo_get_plane_count(bo);
		for (int i = 0; i < num_planes; i++) {
			handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
			strides[i] = gbm_bo_get_stride_for_plane(bo, i);
			offsets[i] = gbm_bo_get_offset(bo, i);
			modifiers[i] = modifiers[0];
		}

		if (modifiers[0] && modifiers[0] != DRM_FORMAT_MOD_INVALID) {
			flags = DRM_MODE_FB_MODIFIERS;
			printf("Using modifier %" PRIx64 "\n", modifiers[0]);
		}

		ret = drmModeAddFB2WithModifiers(drm_fd, width, height,
				format, handles, strides, offsets,
				modifiers, &fb->fb_id, flags);
	}

	if (ret) {
		if (flags)
			fprintf(stderr, "Modifiers failed!\n");

		memcpy(handles, (uint32_t [4]){gbm_bo_get_handle(bo).u32,0,0,0}, 16);
		memcpy(strides, (uint32_t [4]){gbm_bo_get_stride(bo),0,0,0}, 16);
		memset(offsets, 0, 16);
		ret = drmModeAddFB2(drm_fd, width, height, format,
				handles, strides, offsets, &fb->fb_id, 0);
	}

	if (ret) {
		uint32_t handle = gbm_bo_get_handle(bo).u32;
		uint32_t bpp = gbm_bo_get_bpp(bo);
		uint8_t depth = bpp;
		uint32_t pitch = gbm_bo_get_stride(bo);

		ret = drmModeAddFB(drm_fd, width, height, depth, bpp, pitch,
				handle, &fb->fb_id);

	}

	if (ret) {
		printf("failed to create fb: %s\n", strerror(errno));
		free(fb);
		return NULL;
	}

	gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);

	return fb;
}

static int32_t find_crtc_for_encoder(const drmModeRes *resources,
		const drmModeEncoder *encoder) {
	int i;

	for (i = 0; i < resources->count_crtcs; i++) {
		/* possible_crtcs is a bitmask as described here:
		 * https://dvdhrm.wordpress.com/2012/09/13/linux-drm-mode-setting-api
		 */
		const uint32_t crtc_mask = 1 << i;
		const uint32_t crtc_id = resources->crtcs[i];
		if (encoder->possible_crtcs & crtc_mask) {
			return crtc_id;
		}
	}

	/* no match found */
	return -1;
}

static int32_t find_crtc_for_connector(const struct drm *drm, const drmModeRes *resources,
		const drmModeConnector *connector) {
	int i;

	for (i = 0; i < connector->count_encoders; i++) {
		const uint32_t encoder_id = connector->encoders[i];
		drmModeEncoder *encoder = drmModeGetEncoder(drm->fd, encoder_id);

		if (encoder) {
			const int32_t crtc_id = find_crtc_for_encoder(resources, encoder);

			drmModeFreeEncoder(encoder);
			if (crtc_id != 0) {
				return crtc_id;
			}
		}
	}

	/* no match found */
	return -1;
}

static int get_resources(int fd, drmModeRes **resources)
{
	*resources = drmModeGetResources(fd);
	if (*resources == NULL)
		return -1;
	return 0;
}

#define MAX_DRM_DEVICES 64

static int find_drm_render_device(void)
{
	drmDevicePtr devices[MAX_DRM_DEVICES] = { NULL };
	int num_devices, fd = -1;

	num_devices = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
	if (num_devices < 0) {
		printf("drmGetDevices2 failed: %s\n", strerror(-num_devices));
		return -1;
	}

	for (int i = 0; i < num_devices && fd < 0; i++) {
		drmDevicePtr device = devices[i];

		if (!(device->available_nodes & (1 << DRM_NODE_RENDER)))
			continue;
		fd = open(device->nodes[DRM_NODE_RENDER], O_RDWR);
	}
	drmFreeDevices(devices, num_devices);

	if (fd < 0)
		printf("no drm device found!\n");

	return fd;
}

static int find_drm_device(drmModeRes **resources)
{
	drmDevicePtr devices[MAX_DRM_DEVICES] = { NULL };
	int num_devices, fd = -1;

	num_devices = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
	if (num_devices < 0) {
		printf("drmGetDevices2 failed: %s\n", strerror(-num_devices));
		return -1;
	}

	for (int i = 0; i < num_devices; i++) {
		drmDevicePtr device = devices[i];
		int ret;

		if (!(device->available_nodes & (1 << DRM_NODE_PRIMARY)))
			continue;
		/* OK, it's a primary device. If we can get the
		 * drmModeResources, it means it's also a
		 * KMS-capable device.
		 */
		fd = open(device->nodes[DRM_NODE_PRIMARY], O_RDWR);
		if (fd < 0)
			continue;
		ret = get_resources(fd, resources);
		if (!ret)
			break;
		close(fd);
		fd = -1;
	}
	drmFreeDevices(devices, num_devices);

	if (fd < 0)
		printf("no drm device found!\n");
	return fd;
}

static drmModeConnector * find_drm_connector(int fd, drmModeRes *resources,
						int connector_id)
{
	drmModeConnector *connector = NULL;
	int i;

	if (connector_id >= 0) {
		if (connector_id >= resources->count_connectors)
			return NULL;

		connector = drmModeGetConnector(fd, resources->connectors[connector_id]);
		if (connector && connector->connection == DRM_MODE_CONNECTED)
			return connector;

		drmModeFreeConnector(connector);
		return NULL;
	}

	for (i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector && connector->connection == DRM_MODE_CONNECTED) {
			/* it's connected, let's use this! */
			break;
		}
		drmModeFreeConnector(connector);
		connector = NULL;
	}

	return connector;
}

int init_drm(struct drm *drm, const char *device, const char *mode_str,
		int connector_id, unsigned int vrefresh, unsigned int count, bool nonblocking)
{
	drmModeRes *resources;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	int i, ret, area;

	if (device) {
		drm->fd = open(device, O_RDWR);
		ret = get_resources(drm->fd, &resources);
		if (ret < 0 && errno == EOPNOTSUPP)
			printf("%s does not look like a modeset device\n", device);
	} else {
		drm->fd = find_drm_device(&resources);
	}

	if (drm->fd < 0) {
		printf("could not open drm device\n");
		return -1;
	}

	if (!resources) {
		printf("drmModeGetResources failed: %s\n", strerror(errno));
		return -1;
	}

	/* find a connected connector: */
	connector = find_drm_connector(drm->fd, resources, connector_id);

	if (!connector) {
		/* we could be fancy and listen for hotplug events and wait for
		 * a connector..
		 */
		printf("no connected connector!\n");
		return -1;
	}

	/* find user requested mode: */
	if (mode_str && *mode_str) {
		for (i = 0; i < connector->count_modes; i++) {
			drmModeModeInfo *current_mode = &connector->modes[i];

			if (strcmp(current_mode->name, mode_str) == 0) {
				if (vrefresh == 0 || current_mode->vrefresh == vrefresh) {
					drm->mode = current_mode;
					break;
				}
			}
		}
		if (!drm->mode)
			printf("requested mode not found, using default mode!\n");
	}

	/* find preferred mode or the highest resolution mode: */
	if (!drm->mode) {
		for (i = 0, area = 0; i < connector->count_modes; i++) {
			drmModeModeInfo *current_mode = &connector->modes[i];

			if (current_mode->type & DRM_MODE_TYPE_PREFERRED) {
				drm->mode = current_mode;
				break;
			}

			int current_area = current_mode->hdisplay * current_mode->vdisplay;
			if (current_area > area) {
				drm->mode = current_mode;
				area = current_area;
			}
		}
	}

	if (!drm->mode) {
		printf("could not find mode!\n");
		return -1;
	}

	/* find encoder: */
	for (i = 0; i < resources->count_encoders; i++) {
		encoder = drmModeGetEncoder(drm->fd, resources->encoders[i]);
		if (encoder->encoder_id == connector->encoder_id)
			break;
		drmModeFreeEncoder(encoder);
		encoder = NULL;
	}

	if (encoder) {
		drm->crtc_id = encoder->crtc_id;
	} else {
		int32_t crtc_id = find_crtc_for_connector(drm, resources, connector);
		if (crtc_id == -1) {
			printf("no crtc found!\n");
			return -1;
		}

		drm->crtc_id = crtc_id;
	}

	for (i = 0; i < resources->count_crtcs; i++) {
		if (resources->crtcs[i] == drm->crtc_id) {
			drm->crtc_index = i;
			break;
		}
	}

	drmModeFreeResources(resources);

	drm->connector_id = connector->connector_id;
	drm->count = count;
	drm->nonblocking = nonblocking;

	return 0;
}


#define VOID2U64(x) ((uint64_t)(unsigned long)(x))

static struct drm drm = {
	.kms_out_fence_fd = -1,
};

static int add_connector_property(drmModeAtomicReq *req, uint32_t obj_id,
					const char *name, uint64_t value)
{
	struct connector *obj = drm.connector;
	unsigned int i;
	int prop_id = 0;

	for (i = 0 ; i < obj->props->count_props ; i++) {
		if (strcmp(obj->props_info[i]->name, name) == 0) {
			prop_id = obj->props_info[i]->prop_id;
			break;
		}
	}

	if (prop_id < 0) {
		printf("no connector property: %s\n", name);
		return -EINVAL;
	}

	return drmModeAtomicAddProperty(req, obj_id, prop_id, value);
}

static int add_crtc_property(drmModeAtomicReq *req, uint32_t obj_id,
				const char *name, uint64_t value)
{
	struct crtc *obj = drm.crtc;
	unsigned int i;
	int prop_id = -1;

	for (i = 0 ; i < obj->props->count_props ; i++) {
		if (strcmp(obj->props_info[i]->name, name) == 0) {
			prop_id = obj->props_info[i]->prop_id;
			break;
		}
	}

	if (prop_id < 0) {
		printf("no crtc property: %s\n", name);
		return -EINVAL;
	}

	return drmModeAtomicAddProperty(req, obj_id, prop_id, value);
}

static int add_plane_property(drmModeAtomicReq *req, uint32_t obj_id,
				const char *name, uint64_t value)
{
	struct plane *obj = drm.plane;
	unsigned int i;
	int prop_id = -1;

	for (i = 0 ; i < obj->props->count_props ; i++) {
		if (strcmp(obj->props_info[i]->name, name) == 0) {
			prop_id = obj->props_info[i]->prop_id;
			break;
		}
	}


	if (prop_id < 0) {
		printf("no plane property: %s\n", name);
		return -EINVAL;
	}

	return drmModeAtomicAddProperty(req, obj_id, prop_id, value);
}

static int drm_atomic_commit(uint32_t fb_id, uint32_t flags)
{
	drmModeAtomicReq *req;
	uint32_t plane_id = drm.plane->plane->plane_id;
	uint32_t blob_id;
	int ret;

	req = drmModeAtomicAlloc();

	if (flags & DRM_MODE_ATOMIC_ALLOW_MODESET) {
		if (add_connector_property(req, drm.connector_id, "CRTC_ID",
						drm.crtc_id) < 0)
				return -1;

		if (drmModeCreatePropertyBlob(drm.fd, drm.mode, sizeof(*drm.mode),
					      &blob_id) != 0)
			return -1;

		if (add_crtc_property(req, drm.crtc_id, "MODE_ID", blob_id) < 0)
			return -1;

		if (add_crtc_property(req, drm.crtc_id, "ACTIVE", 1) < 0)
			return -1;
	}

	add_plane_property(req, plane_id, "FB_ID", fb_id);
	add_plane_property(req, plane_id, "CRTC_ID", drm.crtc_id);
	add_plane_property(req, plane_id, "SRC_X", 0);
	add_plane_property(req, plane_id, "SRC_Y", 0);
	add_plane_property(req, plane_id, "SRC_W", drm.mode->hdisplay << 16);
	add_plane_property(req, plane_id, "SRC_H", drm.mode->vdisplay << 16);
	add_plane_property(req, plane_id, "CRTC_X", 0);
	add_plane_property(req, plane_id, "CRTC_Y", 0);
	add_plane_property(req, plane_id, "CRTC_W", drm.mode->hdisplay);
	add_plane_property(req, plane_id, "CRTC_H", drm.mode->vdisplay);

	if (drm.kms_in_fence_fd != -1) {
		add_crtc_property(req, drm.crtc_id, "OUT_FENCE_PTR",
				VOID2U64(&drm.kms_out_fence_fd));
		add_plane_property(req, plane_id, "IN_FENCE_FD", drm.kms_in_fence_fd);
	}

	ret = drmModeAtomicCommit(drm.fd, req, flags, NULL);
	if (ret)
		goto out;

	if (drm.kms_in_fence_fd != -1) {
		close(drm.kms_in_fence_fd);
		drm.kms_in_fence_fd = -1;
	}

out:
	drmModeAtomicFree(req);

	return ret;
}

static EGLSyncKHR create_fence(const struct egl *egl, int fd)
{
	EGLint attrib_list[] = {
		EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd,
		EGL_NONE,
	};
	EGLSyncKHR fence = egl->eglCreateSyncKHR(egl->display,
			EGL_SYNC_NATIVE_FENCE_ANDROID, attrib_list);
	assert(fence);
	return fence;
}



/* Pick a plane.. something that at a minimum can be connected to
 * the chosen crtc, but prefer primary plane.
 *
 * Seems like there is some room for a drmModeObjectGetNamedProperty()
 * type helper in libdrm..
 */
static int get_plane_id(void)
{
	drmModePlaneResPtr plane_resources;
	uint32_t i, j;
	int ret = -EINVAL;
	int found_primary = 0;

	plane_resources = drmModeGetPlaneResources(drm.fd);
	if (!plane_resources) {
		printf("drmModeGetPlaneResources failed: %s\n", strerror(errno));
		return -1;
	}

	for (i = 0; (i < plane_resources->count_planes) && !found_primary; i++) {
		uint32_t id = plane_resources->planes[i];
		drmModePlanePtr plane = drmModeGetPlane(drm.fd, id);
		if (!plane) {
			printf("drmModeGetPlane(%u) failed: %s\n", id, strerror(errno));
			continue;
		}

		if (plane->possible_crtcs & (1 << drm.crtc_index)) {
			drmModeObjectPropertiesPtr props =
				drmModeObjectGetProperties(drm.fd, id, DRM_MODE_OBJECT_PLANE);

			/* primary or not, this plane is good enough to use: */
			ret = id;

			for (j = 0; j < props->count_props; j++) {
				drmModePropertyPtr p =
					drmModeGetProperty(drm.fd, props->props[j]);

				if ((strcmp(p->name, "type") == 0) &&
						(props->prop_values[j] == DRM_PLANE_TYPE_PRIMARY)) {
					/* found our primary plane, lets use that: */
					found_primary = 1;
				}

				drmModeFreeProperty(p);
			}

			drmModeFreeObjectProperties(props);
		}

		drmModeFreePlane(plane);
	}

	drmModeFreePlaneResources(plane_resources);

	return ret;
}

const struct drm * init_drm_atomic(const char *device, const char *mode_str,
		int connector_id, unsigned int vrefresh, unsigned int count, bool nonblocking)
{
	uint32_t plane_id;
	int ret;

	ret = init_drm(&drm, device, mode_str, connector_id, vrefresh, count, nonblocking);
	if (ret)
		return NULL;

	ret = drmSetClientCap(drm.fd, DRM_CLIENT_CAP_ATOMIC, 1);
	if (ret) {
		printf("no atomic modesetting support: %s\n", strerror(errno));
		return NULL;
	}

	ret = get_plane_id();
	if (!ret) {
		printf("could not find a suitable plane\n");
		return NULL;
	} else {
		plane_id = ret;
	}

	/* We only do single plane to single crtc to single connector, no
	 * fancy multi-monitor or multi-plane stuff.  So just grab the
	 * plane/crtc/connector property info for one of each:
	 */
	drm.plane = calloc(1, sizeof(*drm.plane));
	drm.crtc = calloc(1, sizeof(*drm.crtc));
	drm.connector = calloc(1, sizeof(*drm.connector));

#define get_resource(type, Type, id) do { 					\
		drm.type->type = drmModeGet##Type(drm.fd, id);			\
		if (!drm.type->type) {						\
			printf("could not get %s %i: %s\n",			\
					#type, id, strerror(errno));		\
			return NULL;						\
		}								\
	} while (0)

	get_resource(plane, Plane, plane_id);
	get_resource(crtc, Crtc, drm.crtc_id);
	get_resource(connector, Connector, drm.connector_id);

#define get_properties(type, TYPE, id) do {					\
		uint32_t i;							\
		drm.type->props = drmModeObjectGetProperties(drm.fd,		\
				id, DRM_MODE_OBJECT_##TYPE);			\
		if (!drm.type->props) {						\
			printf("could not get %s %u properties: %s\n", 		\
					#type, id, strerror(errno));		\
			return NULL;						\
		}								\
		drm.type->props_info = calloc(drm.type->props->count_props,	\
				sizeof(*drm.type->props_info));			\
		for (i = 0; i < drm.type->props->count_props; i++) {		\
			drm.type->props_info[i] = drmModeGetProperty(drm.fd,	\
					drm.type->props->props[i]);		\
		}								\
	} while (0)

	get_properties(plane, PLANE, plane_id);
	get_properties(crtc, CRTC, drm.crtc_id);
	get_properties(connector, CONNECTOR, drm.connector_id);

	return &drm;
}






#define POS_LBF {-1.0f, -1.0f, +1.0f}
#define POS_RBF {+1.0f, -1.0f, +1.0f}
#define POS_LTF {-1.0f, +1.0f, +1.0f}
#define POS_RTF {+1.0f, +1.0f, +1.0f}
#define POS_LBB {-1.0f, -1.0f, -1.0f}
#define POS_RBB {+1.0f, -1.0f, -1.0f}
#define POS_LTB {-1.0f, +1.0f, -1.0f}
#define POS_RTB {+1.0f, +1.0f, -1.0f}

#define COLOR_BLACK   {0.0f, 0.0f, 0.0f}
#define COLOR_BLUE    {0.0f, 0.0f, 1.0f}
#define COLOR_CYAN    {0.0f, 1.0f, 1.0f}
#define COLOR_GREEN   {0.0f, 1.0f, 0.0f}
#define COLOR_WHITE   {1.0f, 1.0f, 1.0f}
#define COLOR_YELLOW  {1.0f, 1.0f, 0.0f}
#define COLOR_MAGENTA {1.0f, 0.0f, 1.0f}
#define COLOR_RED     {1.0f, 0.0f, 0.0f}

#define NORMAL_FORWARD  {+0.0f, +0.0f, +1.0f}
#define NORMAL_BACKWARD {+0.0f, +0.0f, -1.0f}
#define NORMAL_RIGHT    {+1.0f, +0.0f, +0.0f}
#define NORMAL_LEFT     {-1.0f, +0.0f, +0.0f}
#define NORMAL_UP       {+0.0f, +1.0f, +0.0f}
#define NORMAL_DOWN     {+0.0f, -1.0f, +0.0f}

#define TEX_LT {0.0f, 1.0f}
#define TEX_RT {1.0f, 1.0f}
#define TEX_LB {0.0f, 0.0f}
#define TEX_RB {1.0f, 0.0f}

const struct vertex vertices[6 * 4] = {
        // front
        { POS_LBF, COLOR_BLUE,    NORMAL_FORWARD, TEX_LT },
        { POS_RBF, COLOR_MAGENTA, NORMAL_FORWARD, TEX_RT },
        { POS_LTF, COLOR_CYAN,    NORMAL_FORWARD, TEX_LB },
        { POS_RTF, COLOR_WHITE,   NORMAL_FORWARD, TEX_RB },
        // back
        { POS_RBB, COLOR_RED,     NORMAL_BACKWARD, TEX_LT },
        { POS_LBB, COLOR_BLACK,   NORMAL_BACKWARD, TEX_RT },
        { POS_RTB, COLOR_YELLOW,  NORMAL_BACKWARD, TEX_LB },
        { POS_LTB, COLOR_GREEN,   NORMAL_BACKWARD, TEX_RB },
        // right
        { POS_RBF, COLOR_MAGENTA, NORMAL_RIGHT, TEX_LT },
        { POS_RBB, COLOR_RED,     NORMAL_RIGHT, TEX_RT },
        { POS_RTF, COLOR_WHITE,   NORMAL_RIGHT, TEX_LB },
        { POS_RTB, COLOR_YELLOW,  NORMAL_RIGHT, TEX_RB },
        // left
        { POS_LBB, COLOR_BLACK,   NORMAL_LEFT, TEX_LT },
        { POS_LBF, COLOR_BLUE,    NORMAL_LEFT, TEX_RT },
        { POS_LTB, COLOR_GREEN,   NORMAL_LEFT, TEX_LB },
        { POS_LTF, COLOR_CYAN,    NORMAL_LEFT, TEX_RB },
        // top
        { POS_LTF, COLOR_CYAN,    NORMAL_UP, TEX_LT },
        { POS_RTF, COLOR_WHITE,   NORMAL_UP, TEX_RT },
        { POS_LTB, COLOR_GREEN,   NORMAL_UP, TEX_LB },
        { POS_RTB, COLOR_YELLOW,  NORMAL_UP, TEX_RB },
        // bottom
        { POS_LBB, COLOR_BLACK,   NORMAL_DOWN, TEX_LT },
        { POS_RBB, COLOR_RED,     NORMAL_DOWN, TEX_RT },
        { POS_LBF, COLOR_BLUE,    NORMAL_DOWN, TEX_LB },
        { POS_RBF, COLOR_MAGENTA, NORMAL_DOWN, TEX_RB },
};

static struct gbm gbm;
static struct egl _egl;
static struct egl *egl = &_egl;

WEAK uint64_t
gbm_bo_get_modifier(struct gbm_bo *bo);

WEAK struct gbm_surface *
gbm_surface_create_with_modifiers(struct gbm_device *gbm,
                                  uint32_t width, uint32_t height,
                                  uint32_t format,
                                  const uint64_t *modifiers,
                                  const unsigned int count);
WEAK struct gbm_bo *
gbm_bo_create_with_modifiers(struct gbm_device *gbm,
                             uint32_t width, uint32_t height,
                             uint32_t format,
                             const uint64_t *modifiers,
                             const unsigned int count);

static struct gbm_bo * init_bo(uint64_t modifier)
{
	struct gbm_bo *bo = NULL;

	if (gbm_bo_create_with_modifiers) {
		bo = gbm_bo_create_with_modifiers(gbm.dev,
						  gbm.width, gbm.height,
						  gbm.format,
						  &modifier, 1);
	}

	if (!bo) {
		if (modifier != DRM_FORMAT_MOD_LINEAR) {
			fprintf(stderr, "Modifiers requested but support isn't available\n");
			return NULL;
		}

		bo = gbm_bo_create(gbm.dev,
				   gbm.width, gbm.height,
				   gbm.format,
				   GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	}

	if (!bo) {
		printf("failed to create gbm bo\n");
		return NULL;
	}

	return bo;
}



static struct gbm * init_surface(uint64_t modifier)
{
	if (gbm_surface_create_with_modifiers) {
		gbm.surface = gbm_surface_create_with_modifiers(gbm.dev,
								gbm.width, gbm.height,
								gbm.format,
								&modifier, 1);

	}

	if (!gbm.surface) {
		if (modifier != DRM_FORMAT_MOD_LINEAR) {
			fprintf(stderr, "Modifiers requested but support isn't available\n");
			return NULL;
		}
		gbm.surface = gbm_surface_create(gbm.dev,
						gbm.width, gbm.height,
						gbm.format,
						GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

	}

	if (!gbm.surface) {
		printf("failed to create gbm surface\n");
		return NULL;
	}

	return &gbm;
}

const struct gbm * init_gbm(int drm_fd, int w, int h, uint32_t format,
		uint64_t modifier, bool surfaceless)
{
	gbm.dev = gbm_create_device(drm_fd);
	if (!gbm.dev)
		return NULL;

        gbm.format = format;
	gbm.surface = NULL;

	gbm.width = w;
	gbm.height = h;


	return init_surface(modifier);
}

static bool has_ext(const char *extension_list, const char *ext)
{
	const char *ptr = extension_list;
	int len = strlen(ext);

	if (ptr == NULL || *ptr == '\0')
		return false;

	while (true) {
		ptr = strstr(ptr, ext);
		if (!ptr)
			return false;

		if (ptr[len] == ' ' || ptr[len] == '\0')
			return true;

		ptr += len;
	}
}

static int
match_config_to_visual(EGLDisplay egl_display,
		       EGLint visual_id,
		       EGLConfig *configs,
		       int count)
{
	int i;

	for (i = 0; i < count; ++i) {
		EGLint id;

		if (!eglGetConfigAttrib(egl_display,
				configs[i], EGL_NATIVE_VISUAL_ID,
				&id))
			continue;

		if (id == visual_id)
			return i;
	}

	return -1;
}

static bool
egl_choose_config(EGLDisplay egl_display, const EGLint *attribs,
                  EGLint visual_id, EGLConfig *config_out)
{
	EGLint count = 0;
	EGLint matched = 0;
	EGLConfig *configs;
	int config_index = -1;

	if (!eglGetConfigs(egl_display, NULL, 0, &count) || count < 1) {
		printf("No EGL configs to choose from.\n");
		return false;
	}
	configs = malloc(count * sizeof *configs);
	if (!configs)
		return false;

	if (!eglChooseConfig(egl_display, attribs, configs,
			      count, &matched) || !matched) {
		printf("No EGL configs with appropriate attributes.\n");
		goto out;
	}

	if (!visual_id)
		config_index = 0;

	if (config_index == -1)
		config_index = match_config_to_visual(egl_display,
						      visual_id,
						      configs,
						      matched);

	if (config_index != -1)
		*config_out = configs[config_index];

out:
	free(configs);
	if (config_index == -1)
		return false;

	return true;
}

static bool
create_framebuffer(const struct egl *egl, struct gbm_bo *bo,
		struct framebuffer *fb) {
	assert(egl->eglCreateImageKHR);
	assert(bo);
	assert(fb);

	// 1. Create EGLImage.
	int fd = gbm_bo_get_fd(bo);
	if (fd < 0) {
		printf("failed to get fd for bo: %d\n", fd);
		return false;
	}

	EGLint khr_image_attrs[17] = {
		EGL_WIDTH, gbm_bo_get_width(bo),
		EGL_HEIGHT, gbm_bo_get_height(bo),
		EGL_LINUX_DRM_FOURCC_EXT, (int)gbm_bo_get_format(bo),
		EGL_DMA_BUF_PLANE0_FD_EXT, fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, gbm_bo_get_stride(bo),
		EGL_NONE, EGL_NONE,	/* modifier lo */
		EGL_NONE, EGL_NONE,	/* modifier hi */
		EGL_NONE,
	};

	if (egl->modifiers_supported) {
		const uint64_t modifier = gbm_bo_get_modifier(bo);
		size_t attrs_index = 12;
		khr_image_attrs[attrs_index++] =
		    EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
		khr_image_attrs[attrs_index++] = modifier & 0xfffffffful;
		khr_image_attrs[attrs_index++] =
		    EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
		khr_image_attrs[attrs_index++] = modifier >> 32;
	}

	fb->image = egl->eglCreateImageKHR(egl->display, EGL_NO_CONTEXT,
			EGL_LINUX_DMA_BUF_EXT, NULL /* no client buffer */,
			khr_image_attrs);

	if (fb->image == EGL_NO_IMAGE_KHR) {
		printf("failed to make image from buffer object\n");
		return false;
	}

	// EGLImage takes the fd ownership.
	close(fd);

	// 2. Create GL texture and framebuffer.
	glGenTextures(1, &fb->tex);
	glBindTexture(GL_TEXTURE_2D, fb->tex);
	egl->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, fb->image);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &fb->fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb->fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
			fb->tex, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("failed framebuffer check for created target buffer\n");
		glDeleteFramebuffers(1, &fb->fb);
		glDeleteTextures(1, &fb->tex);
		return false;
	}

	return true;
}

struct egl * init_egl(const struct gbm *gbm, int samples)
{
	EGLint major, minor;

	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SAMPLES, samples,
		EGL_NONE
	};
	const char *egl_exts_client, *egl_exts_dpy, *gl_exts;

#define get_proc_client(ext, name) do { \
		if (has_ext(egl_exts_client, #ext)) \
			egl->name = (void *)eglGetProcAddress(#name); \
	} while (0)
#define get_proc_dpy(ext, name) do { \
		if (has_ext(egl_exts_dpy, #ext)) \
			egl->name = (void *)eglGetProcAddress(#name); \
	} while (0)

#define get_proc_gl(ext, name) do { \
		if (has_ext(gl_exts, #ext)) \
			egl->name = (void *)eglGetProcAddress(#name); \
	} while (0)

	egl_exts_client = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	get_proc_client(EGL_EXT_platform_base, eglGetPlatformDisplayEXT);

	if (egl->eglGetPlatformDisplayEXT) {
		egl->display = egl->eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR,
				gbm->dev, NULL);
	} else {
		egl->display = eglGetDisplay((EGLNativeDisplayType)gbm->dev);
	}

	if (!eglInitialize(egl->display, &major, &minor)) {
		printf("failed to initialize\n");
		return NULL;
	}

	egl_exts_dpy = eglQueryString(egl->display, EGL_EXTENSIONS);
	get_proc_dpy(EGL_KHR_image_base, eglCreateImageKHR);
	get_proc_dpy(EGL_KHR_image_base, eglDestroyImageKHR);
	get_proc_dpy(EGL_KHR_fence_sync, eglCreateSyncKHR);
	get_proc_dpy(EGL_KHR_fence_sync, eglDestroySyncKHR);
	get_proc_dpy(EGL_KHR_fence_sync, eglWaitSyncKHR);
	get_proc_dpy(EGL_KHR_fence_sync, eglClientWaitSyncKHR);
	get_proc_dpy(EGL_ANDROID_native_fence_sync, eglDupNativeFenceFDANDROID);

	egl->modifiers_supported = has_ext(egl_exts_dpy,
					   "EGL_EXT_image_dma_buf_import_modifiers");

	printf("Using display %p with EGL version %d.%d\n",
			egl->display, major, minor);

	printf("===================================\n");
	printf("EGL information:\n");
	printf("  version: \"%s\"\n", eglQueryString(egl->display, EGL_VERSION));
	printf("  vendor: \"%s\"\n", eglQueryString(egl->display, EGL_VENDOR));
	printf("  client extensions: \"%s\"\n", egl_exts_client);
	printf("  display extensions: \"%s\"\n", egl_exts_dpy);
	printf("===================================\n");

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		printf("failed to bind api EGL_OPENGL_ES_API\n");
		return NULL;
	}

	if (!egl_choose_config(egl->display, config_attribs, gbm->format,
                               &egl->config)) {
		printf("failed to choose config\n");
		return NULL;
	}

	egl->context = eglCreateContext(egl->display, egl->config,
			EGL_NO_CONTEXT, context_attribs);
	if (egl->context == EGL_NO_CONTEXT) {
		printf("failed to create context\n");
		return NULL;
	}

	if (!gbm->surface) {
		egl->surface = EGL_NO_SURFACE;
	} else {
		egl->surface = eglCreateWindowSurface(egl->display, egl->config,
				(EGLNativeWindowType)gbm->surface, NULL);
		if (egl->surface == EGL_NO_SURFACE) {
			printf("failed to create egl surface\n");
			return NULL;
		}
	}

	/* connect the context to the surface */
	eglMakeCurrent(egl->display, egl->surface, egl->surface, egl->context);

	gl_exts = (char *) glGetString(GL_EXTENSIONS);
	printf("OpenGL ES 2.x information:\n");
	printf("  version: \"%s\"\n", glGetString(GL_VERSION));
	printf("  shading language version: \"%s\"\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	printf("  vendor: \"%s\"\n", glGetString(GL_VENDOR));
	printf("  renderer: \"%s\"\n", glGetString(GL_RENDERER));
	printf("  extensions: \"%s\"\n", gl_exts);
	printf("===================================\n");

	get_proc_gl(GL_OES_EGL_image, glEGLImageTargetTexture2DOES);

	get_proc_gl(GL_AMD_performance_monitor, glGetPerfMonitorGroupsAMD);
	get_proc_gl(GL_AMD_performance_monitor, glGetPerfMonitorCountersAMD);
	get_proc_gl(GL_AMD_performance_monitor, glGetPerfMonitorGroupStringAMD);
	get_proc_gl(GL_AMD_performance_monitor, glGetPerfMonitorCounterStringAMD);
	get_proc_gl(GL_AMD_performance_monitor, glGetPerfMonitorCounterInfoAMD);
	get_proc_gl(GL_AMD_performance_monitor, glGenPerfMonitorsAMD);
	get_proc_gl(GL_AMD_performance_monitor, glDeletePerfMonitorsAMD);
	get_proc_gl(GL_AMD_performance_monitor, glSelectPerfMonitorCountersAMD);
	get_proc_gl(GL_AMD_performance_monitor, glBeginPerfMonitorAMD);
	get_proc_gl(GL_AMD_performance_monitor, glEndPerfMonitorAMD);
	get_proc_gl(GL_AMD_performance_monitor, glGetPerfMonitorCounterDataAMD);

	if (!gbm->surface) {
		for (unsigned i = 0; i < ARRAY_SIZE(gbm->bos); i++) {
			if (!create_framebuffer(egl, gbm->bos[i], &egl->fbs[i])) {
				printf("failed to create framebuffer\n");
				return NULL;
			}
		}
	}

	return egl;
}

int create_program(const char *vs_src, const char *fs_src, GLuint *out_program)
{
	GLuint vertex_shader, fragment_shader, program;
	GLint ret;

	vertex_shader = glCreateShader(GL_VERTEX_SHADER);
	if (vertex_shader == 0) {
		printf("vertex shader creation failed!:\n");
		return -1;
	}

	glShaderSource(vertex_shader, 1, &vs_src, NULL);
	glCompileShader(vertex_shader);

	glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &ret);
	if (!ret) {
		char *log;

		printf("vertex shader compilation failed!:\n");
		glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &ret);
		if (ret > 1) {
			log = malloc(ret);
			glGetShaderInfoLog(vertex_shader, ret, NULL, log);
			printf("%s", log);
			free(log);
		}

		return -1;
	}

	fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
	if (fragment_shader == 0) {
		printf("fragment shader creation failed!:\n");
		return -1;
	}
	glShaderSource(fragment_shader, 1, &fs_src, NULL);
	glCompileShader(fragment_shader);

	glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &ret);
	if (!ret) {
		char *log;

		printf("fragment shader compilation failed!:\n");
		glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetShaderInfoLog(fragment_shader, ret, NULL, log);
			printf("%s", log);
			free(log);
		}

		return -1;
	}

	program = glCreateProgram();

	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);

	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &ret);
	if (!ret) {
		char *log;

		printf("program linking failed!:\n");
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = malloc(ret);
			glGetProgramInfoLog(program, ret, NULL, log);
			printf("%s", log);
			free(log);
		}

		return -1;
	}

	*out_program = program;
	return 0;
}


int buf_to_fd(const struct gbm *gbm,
              uint32_t width, uint32_t height, uint32_t bpp, const void *ptr,
              uint32_t *pstride, uint64_t *modifier)
{
	const uint8_t *src = (const uint8_t *)ptr;
	uint32_t format;
	struct gbm_bo *bo;
	void *map_data = NULL;
	uint32_t stride;
	uint8_t *map;
	int fd;

	switch (bpp) {
	case 1:
		format = GBM_FORMAT_R8;
		break;
	case 2:
		format = GBM_FORMAT_GR88;
		break;
	case 4:
		format = GBM_FORMAT_ABGR8888;
		break;
	default:
		assert(!"unreachable");
		return -1;
	}

	bo = gbm_bo_create(gbm->dev, width, height, format, GBM_BO_USE_LINEAR);

	map = gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_WRITE, &stride, &map_data);

	for (uint32_t i = 0; i < height; i++) {
		memcpy(&map[stride * i], &src[width * bpp * i], width * bpp);
	}

	gbm_bo_unmap(bo, map_data);

	fd = gbm_bo_get_fd(bo);

	if (gbm_bo_get_modifier)
		*modifier = gbm_bo_get_modifier(bo);
	else
		*modifier = DRM_FORMAT_MOD_LINEAR;

	/* we have the fd now, no longer need the bo: */
	gbm_bo_destroy(bo);

	*pstride = stride;

	return fd;
}
