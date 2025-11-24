
#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>


class GBM;

struct framebuffer {
	EGLImageKHR image;
	GLuint tex;
	GLuint fb;
};


class EGL {
public:
	EGL(const GBM &gbm);
	EGLSyncKHR create_fence(int fd);

public:
	EGLDisplay display;
	EGLConfig config;
	EGLContext context;
	EGLSurface surface;

	PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT;
	PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
	PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
	PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
	PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
	PFNEGLWAITSYNCKHRPROC eglWaitSyncKHR;
	PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;
	PFNEGLDUPNATIVEFENCEFDANDROIDPROC eglDupNativeFenceFDANDROID;

	bool modifiers_supported;
};


struct drm_fb {
	struct gbm_bo *bo;
	uint32_t fb_id;
};
struct drm_fb * drm_fb_get_from_bo(struct gbm_bo *bo);
