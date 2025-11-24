#include "gbm_util.h"

#include "drm_util.h"
#include <drm_fourcc.h>


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
