

#include <turbojpeg.h>
#include <vector>
#include <string>
#include <SDL3/SDL.h>
#include <cstddef>


#define LOADER_GL_PIXEL_FORMAT GL_RGB

static tjhandle g_tj = nullptr;


bool init_img_loader() {
    if (!g_tj) g_tj = tjInitDecompress();
    return true;
}


bool _load_image(unsigned char *&pixeldata_out, size_t &pixeldata_len_out, const std::vector<unsigned char> &filebuf_in, const std::string &path_in, int &width, int &height) {
    int subsamp, colorspace;
    if (tjDecompressHeader3(g_tj, filebuf_in.data(), filebuf_in.size(),
                            &width, &height, &subsamp, &colorspace) != 0) {
        SDL_Log("TurboJPEG header read failed: %s", tjGetErrorStr());
        return false;
    }

    pixeldata_out = (unsigned char*)malloc(width*height*3*sizeof(unsigned char));
    if (!pixeldata_out) {
        SDL_Log("Out of memory");
        return false;
    }

    if (tjDecompress2(g_tj, filebuf_in.data(), filebuf_in.size(),
                    pixeldata_out, width, 0, height,
                    TJPF_RGB, TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE | TJFLAG_BOTTOMUP) != 0) {
        SDL_Log("TurboJPEG decompress failed: %s", tjGetErrorStr());
        free(pixeldata_out);
        return false;
    }

    return true;
}

void _free_pixeldata(unsigned char *pixeldata, size_t pixeldata_len) {
    if(pixeldata)
        free(pixeldata);
}

void loader_cleanup() {}