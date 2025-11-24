#pragma once

#include <stdint.h>

class DRM;
class GBM;
class EGL;

class GL {
public:
    GL(DRM &drm, GBM &gbm, EGL &egl);
    void gl_render(float fade_amount);

private:
    DRM &drm_ref;
    GBM &gbm_ref;
    EGL &egl_ref;

    struct gbm_bo *bo;
	uint32_t flags;
};

