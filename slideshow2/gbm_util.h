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
