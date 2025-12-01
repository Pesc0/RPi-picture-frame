#pragma once

#include <gbm.h>

class DRM;

class GBM {
public:
    GBM(const DRM &drm);

public:
	struct gbm_device *dev;
	struct gbm_surface *surface;
	uint32_t format;
	int width, height;
};


struct drm_fb {
	struct gbm_bo *bo;
	uint32_t fb_id;
};
struct drm_fb * drm_fb_get_from_bo(struct gbm_bo *bo);
