#include "load_image.h"

#include <fstream>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <cstddef>

//#include <algorithm>


bool _load_image(unsigned char *&pixeldata_out, size_t &pixeldata_len_out, const std::vector<unsigned char> &filebuf_in, const std::string &path_in, int &width, int &height);
void _free_pixeldata(unsigned char *pixeldata, size_t pixeldata_len);

#ifdef USE_STB_IMAGE
    #include <loader_stb.cpp>
#endif
#ifdef USE_TURBO_JPEG
    #include <loader_turbojpeg.cpp>
#endif
#ifdef USE_MMAL
    #include <loader_mmal.cpp>
#endif
#ifdef USE_V4L2
    #include <loader_v4l2.cpp>
#endif

#if !defined(LOADER_GL_PIXEL_FORMAT) 
    #define LOADER_GL_PIXEL_FORMAT GL_RGB
#endif


#ifdef DEBUG
    class ScopedTimer {
    public:
        ScopedTimer(const std::string &name) : _name(name) {
            start = SDL_GetPerformanceCounter();
        }

        ~ScopedTimer() {
            end = SDL_GetPerformanceCounter();
            SDL_Log("%s in %fs", _name.c_str(), (end - start) / 1000000000.0f); //nanoseconds to seconds
        }

        Uint64 start, end;
        const std::string _name;
    };
#endif


bool load_image(const std::string& path, GLenum texture_unit) { 
    std::vector<unsigned char> filebuf;

    {
        #ifdef DEBUG
            ScopedTimer timer("read file"); 
        #endif

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            SDL_Log("Failed to open %s", path.c_str());
            return false;
        }

        const auto size = file.tellg();
        filebuf.resize(size);

        file.seekg(0, std::ios::beg);
        if (!file.read((char*)filebuf.data(), size)) {
            SDL_Log("Failed to read %s", path.c_str());
            return false;
        }
    }

    int width = 0, height = 0;
    unsigned char* pixeldata = nullptr;
    size_t pixeldata_len = 0;    

    {
        #ifdef DEBUG
            ScopedTimer timer("decoded image"); 
        #endif

        if (!_load_image(pixeldata, pixeldata_len, filebuf, path, width, height)) {
            _free_pixeldata(pixeldata, pixeldata_len);
            return false;
        }
    }

    {
        #ifdef DEBUG
            ScopedTimer timer("uploaded to GPU"); 
        #endif
        
        if (!pixeldata || width <= 0 || height <= 0) {
            SDL_Log("Invalid decoded data for GL upload ptr:%d w:%d h:%d", pixeldata, width, height);
            return false;
        }

        glActiveTexture(texture_unit); // bind texture unit, texture is already bound inside it
        //glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // avoid padding issues
        glTexImage2D(GL_TEXTURE_2D, 0, LOADER_GL_PIXEL_FORMAT, width, height, 0, LOADER_GL_PIXEL_FORMAT, GL_UNSIGNED_BYTE, pixeldata); //glTexSubImage2D does not work on RPi

        _free_pixeldata(pixeldata, pixeldata_len);
    }

    return true;
}