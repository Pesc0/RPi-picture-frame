#pragma once

#include <string>
#include <SDL3/SDL_opengles2.h>

bool load_image(const std::string& path, GLenum texture_unit);