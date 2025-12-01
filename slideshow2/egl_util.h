
#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>


class GBM;


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
	PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
	PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
	PFNEGLWAITSYNCKHRPROC eglWaitSyncKHR;
	PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;
	PFNEGLDUPNATIVEFENCEFDANDROIDPROC eglDupNativeFenceFDANDROID;
};


