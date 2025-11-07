#include "load_image.h"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstddef>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengles2.h>



bool _init_img_loader();
bool _load_image(unsigned char *&pixeldata_out, size_t &pixeldata_len_out, const std::vector<unsigned char> &filebuf_in, const std::string &path_in, int &width, int &height);
void _free_pixeldata(unsigned char *pixeldata, size_t pixeldata_len);
void _loader_cleanup();

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



ImageLoader::ImageLoader(const std::string& path) : folder_path(path) { 
    init_success = true;

    if (!_init_img_loader()) { init_success = false; return; }
    if (!load_file_list()) { init_success = false; return; }
    if (!load_image(img_files[0], GL_TEXTURE0)) { init_success = false; return; }
}

ImageLoader::~ImageLoader() { _loader_cleanup(); }


bool ImageLoader::load_file_list() {
    namespace fs = std::filesystem;
    std::vector<std::string> imgs_found;
    try {
        if (fs::exists(folder_path) && fs::is_directory(folder_path)) {
            for (const auto& entry : fs::directory_iterator(folder_path)) {
                //jpgs load in 1.4-1.6 seconds, pngs in 1.0-1.2 seconds but take much more space in filesystem
                if (entry.is_regular_file() && entry.path().extension().string() == ".jpg") 
                {
                    imgs_found.push_back(entry.path().string());
#ifdef DEBUG
                    SDL_Log("found: %s", entry.path().string().c_str());
#endif
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        SDL_Log("Filesystem error: %s", e.what());
        return false;
    }

    if (imgs_found.empty()) {
        SDL_Log("No files found in %s", folder_path.c_str());
        return false;
    }

    img_files = imgs_found;
    //NOTE: back_texture_file_idx is now in an invalid state. but since it is read only at FADE_DONE, and 
    // FADE_DONE also invalidates new_image_loaded_since_fade, a new back_texture_file_idx will get assigned anyway.
    return true;
}


int ImageLoader::get_file_idx(const std::string &path) {
    auto pos = std::find(img_files.begin(), img_files.end(), path);
    return pos != img_files.end() ? std::distance(img_files.begin(), pos) : 0;
}


bool ImageLoader::load_image_to_back_texture(int file_idx) {
    std::string path = img_files[file_idx];

    bool success = load_image(path, current_active_texture == 1 ? GL_TEXTURE0 : GL_TEXTURE1);
    if (success) {
        tex_loaded_filenames[!current_active_texture] = path;
        new_image_loaded = true;
    }
    return success;
}


bool ImageLoader::load_next_image() { //search forwards until an image can be loaded
    int curr_file_idx = get_file_idx(tex_loaded_filenames[current_active_texture]);

    bool success = false;
    int attempts = 0;
    while (!success && attempts < img_files.size()) {
        success = load_image_to_back_texture((curr_file_idx+1+attempts)%img_files.size());
        attempts++;
    }
    
    return success;
}


bool ImageLoader::load_prev_image() { //search backwards until an image can be loaded
    int curr_file_idx = get_file_idx(tex_loaded_filenames[current_active_texture]);

    bool success = false;
    int attempts = 0;
    while (!success && attempts < img_files.size()) {
        success = load_image_to_back_texture((curr_file_idx+img_files.size()-1-attempts)%img_files.size());
        attempts++;
    }
    
    return success;
}


void ImageLoader::switch_active_texture() {
    current_active_texture = !current_active_texture;
    new_image_loaded = false;
}