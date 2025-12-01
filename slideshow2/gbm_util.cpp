#include "gbm_util.h"

#include "drm_util.h"
#include <drm_fourcc.h>

#include <cinttypes>
#include <cstring>
#include <stdexcept>

GBM::GBM(const DRM &drm)
{
	this->dev = gbm_create_device(drm.fd);
	if (!this->dev) throw std::runtime_error("Failed to create gbm device");

    this->format = DRM_FORMAT_XRGB8888;
	this->width = drm.mode->hdisplay;
	this->height = drm.mode->vdisplay;

	this->surface = gbm_surface_create(this->dev,
						this->width, this->height,
						this->format,
						GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

	if (!this->surface) {
		throw std::runtime_error("failed to create gbm surface\n");
	}
}





static void
drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
	int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
	struct drm_fb *fb = (drm_fb*)data;

	if (fb->fb_id)
		drmModeRmFB(drm_fd, fb->fb_id);

	free(fb);
}

struct drm_fb * drm_fb_get_from_bo(struct gbm_bo *bo)
{
	int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
	struct drm_fb *fb = (drm_fb*)gbm_bo_get_user_data(bo);
	uint32_t width, height, format,
		 strides[4] = {0}, handles[4] = {0},
		 offsets[4] = {0}, flags = 0;
	int ret = -1;

	if (fb)
		return fb;

	fb = (drm_fb*)calloc(1, sizeof *fb);
	fb->bo = bo;

	width = gbm_bo_get_width(bo);
	height = gbm_bo_get_height(bo);
	format = gbm_bo_get_format(bo);


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


