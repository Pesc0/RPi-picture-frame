#include "load_image.h"


#ifdef USE_STB_IMAGE
    #include <stb_image.h>
#endif
#ifdef USE_TURBO_JPEG
    #include <turbojpeg.h>
#endif


#include <fstream>
#include <vector>
#include <SDL3/SDL.h>


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

    int width, height;
    unsigned char* pixeldata;
    {
        #ifdef DEBUG
            ScopedTimer timer("decoded image"); 
        #endif

        #ifdef USE_STB_IMAGE
            int channels;
            stbi_set_flip_vertically_on_load(1);
            pixeldata = stbi_load_from_memory(filebuf.data(), filebuf.size(), &width, &height, &channels, STBI_rgb); 
            if (!pixeldata) {
                SDL_Log("Failed to load image %s: %s", path.c_str(), stbi_failure_reason());
                return false;
            }
        #endif

        #ifdef USE_TURBO_JPEG
            static tjhandle g_tj = nullptr;
            if (!g_tj) g_tj = tjInitDecompress();

            int subsamp, colorspace;
            if (tjDecompressHeader3(g_tj, filebuf.data(), filebuf.size(),
                                    &width, &height, &subsamp, &colorspace) != 0) {
                SDL_Log("TurboJPEG header read failed: %s", tjGetErrorStr());
                return false;
            }

            pixeldata = (unsigned char*)malloc(width*height*3*sizeof(unsigned char));

            if (tjDecompress2(g_tj, filebuf.data(), filebuf.size(),
                            pixeldata, width, 0, height,
                            TJPF_RGB, TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE | TJFLAG_BOTTOMUP) != 0) {
                SDL_Log("TurboJPEG decompress failed: %s", tjGetErrorStr());
                return false;
            }
        #endif
    }

    {
        #ifdef DEBUG
            ScopedTimer timer("uploaded to GPU"); 
        #endif

        glActiveTexture(texture_unit); // bind texture unit, texture is already bound inside it
        //glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // avoid padding issues
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixeldata); //glTexSubImage2D does not work on RPi
    }

    if(pixeldata) free(pixeldata);

    return true;
}