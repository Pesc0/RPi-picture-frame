#pragma once

#include <string>
#include <SDL3/SDL_opengles2.h>

bool init_img_loader();
bool load_image(const std::string& path, GLenum texture_unit);
void loader_cleanup(); //to be called on program exit