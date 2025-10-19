#include <stb_image.h>
#include <vector>
#include <string>
#include <SDL3/SDL.h>
#include <cstddef>

#define LOADER_GL_PIXEL_FORMAT GL_RGB


bool init_img_loader() {
    stbi_set_flip_vertically_on_load(1);
    return true;
}


bool _load_image(unsigned char *&pixeldata_out, size_t &pixeldata_len_out, const std::vector<unsigned char> &filebuf_in, const std::string &path_in, int &width, int &height) {
    int channels;
    pixeldata_out = stbi_load_from_memory(filebuf_in.data(), filebuf_in.size(), &width, &height, &channels, STBI_rgb); 
    if (!pixeldata_out) {
        SDL_Log("Failed to load image %s: %s", path_in.c_str(), stbi_failure_reason());
        return false;
    }
    return true;
}


void _free_pixeldata(unsigned char *pixeldata, size_t pixeldata_len) {
    if(pixeldata)
        stbi_image_free(pixeldata);
}

void loader_cleanup() {}