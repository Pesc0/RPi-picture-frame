
#include "SDL_GL_window.h"
#include "load_image.h"

#include <math.h>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>




#define DEFAULT_IMG_DISPLAY_TIME 60.0f 
#define DEFAULT_IMG_FADE_TIME 0.5f
#define DEFAULT_IMG_FOLDER_PATH "/tmp"


/* GLOBALS */
std::vector<std::string> files;
std::vector<std::string> broken_files; //files that can't be loaded //FIXME clear every now and then
int curr_file_idx = 0;
int back_texture_file_idx; 
bool new_image_loaded_since_fade = false;

bool current_active_texture = 0; 

enum State { DISPLAY, FADING };
State curr_state = DISPLAY;
float curr_state_time_spent = 0.0f;
bool paused = false;




bool load_file_list(const std::string& folderPath) {
    namespace fs = std::filesystem;
    std::string curr_file = files.empty() ? "" : files[curr_file_idx];
    files.clear();
    try {
        if (fs::exists(folderPath) && fs::is_directory(folderPath)) {
            for (const auto& entry : fs::directory_iterator(folderPath)) {
                //jpgs load in 1.4-1.6 seconds, pngs in 1.0-1.2 seconds but take much more space in filesystem
                if (entry.is_regular_file() && entry.path().extension().string() == ".jpg"
                        && std::find(broken_files.begin(), broken_files.end(), entry.path().string()) == broken_files.end()) //file is not listed in broken_files
                {
                    files.push_back(entry.path().string());
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

    if (files.empty()) {
        SDL_Log("No files found in %s", folderPath.c_str());
        return false;
    }

    auto pos = std::find(files.begin(), files.end(), curr_file);
    if (pos != files.end()) {
        curr_file_idx = std::distance(files.begin(), pos);
    }
    else {
        curr_file_idx = 0;
    }

#ifdef DEBUG
    SDL_Log("idx: %d", curr_file_idx);
#endif

    //NOTE: back_texture_file_idx is now in an invalid state. but since it is read only at FADE_DONE, and 
    // FADE_DONE also invalidates new_image_loaded_since_fade, a new back_texture_file_idx will get assigned anyway.
    return true;
}



bool load_image_to_back_texture(int file_idx) {
    back_texture_file_idx = file_idx;
    std::string path = files[back_texture_file_idx];
    if (current_active_texture) //texture1 is on screen
        return load_image(path, GL_TEXTURE0);
    else
        return load_image(path, GL_TEXTURE1);
}

bool load_next_image() { //search forwards until an image can be loaded
    //next image may have already been loaded, thus we can exit early. 
    //does not apply to prev image, since that is done only on command and shown immediately.
    //we can safely return success, since failure of finding a next image means program exit. 
    //so if the program is still running and has already new_image_loaded_since_fade it means it has been found.
    if (new_image_loaded_since_fade) return true; 

    bool success = false;
    int attempts = 0;
    while (!success && attempts < files.size()) {
        success = load_image_to_back_texture((curr_file_idx+1+attempts)%files.size());
        attempts++;
    }
    
    new_image_loaded_since_fade = success;
    return success;
}

bool load_prev_image() { //search backwards until an image can be loaded
    bool success = false;
    int attempts = 0;
    while (!success && attempts < files.size()) {
        success = load_image_to_back_texture((curr_file_idx+files.size()-1-attempts)%files.size());
        attempts++;
    }
    
    new_image_loaded_since_fade = success;
    return success;
}



int main(int, char**)
{
    const char* env_img_display_time = getenv("IMG_DISPLAY_TIME");
    const float img_display_time_s = env_img_display_time != nullptr ? std::stof(env_img_display_time) : DEFAULT_IMG_DISPLAY_TIME;

    const char* env_img_fade_time = getenv("IMG_FADE_TIME");
    const float img_fade_time_s = env_img_fade_time != nullptr ? std::stof(env_img_fade_time) : DEFAULT_IMG_FADE_TIME;

    const char* env_folder_path = getenv("IMG_FOLDER_PATH");
    const std::string folder_path = env_folder_path != nullptr ? env_folder_path : DEFAULT_IMG_FOLDER_PATH;

    SDL_GL_window my_window;

    //declared here to allow goto
    int event_fd;
    Uint64 prevTime;
    bool done;

    if (!init_img_loader()) goto exit_error;

    event_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (event_fd < 0) { perror("open"); goto exit_error; }

    // first render
    if (!load_file_list(folder_path)) goto exit_error;
    if (!load_image(files[curr_file_idx], GL_TEXTURE0)) goto exit_error; //load initial image on texture zero
    my_window.render(0.0f);
    my_window.render(0.0f);
    prevTime = SDL_GetPerformanceCounter(); 
    SDL_Delay(100);

    // Main loop
    done = false;
    while (!done)
    {
        Uint64 crntTime = SDL_GetPerformanceCounter();
        float ts = (crntTime - prevTime) / 1000000000.0f; //nanoseconds to seconds
        prevTime = crntTime;

        if (my_window.wants_to_close()) done = true;

        struct input_event ev;
        while (read(event_fd, &ev, sizeof(ev)) > 0) {
            if (ev.type == EV_KEY && ev.value == 1) {
                switch (ev.code)
                {
                case 57: //space
                    paused = !paused; //FIXME paused indicator
                    break; 

                case 105: //left = prev
                    if (curr_state == FADING) break;
                    if(!load_prev_image()) goto exit_error;
                    curr_state_time_spent = img_fade_time_s; //jump directly to next image, don't fade
                    curr_state = FADING;
                    break;

                case 106: //right = next
                    if (curr_state == FADING) break;
                    if(!load_next_image()) goto exit_error;
                    curr_state_time_spent = img_fade_time_s; //jump directly to next image, don't fade
                    curr_state = FADING;
                    break;

                }
            }
        }

        switch (curr_state)
        {
        case DISPLAY:
            if (!paused) {
                curr_state_time_spent += ts;
                if (curr_state_time_spent > img_display_time_s) {
                    curr_state_time_spent = 0;
                    curr_state = FADING;
                    break;
                }
                else if (!new_image_loaded_since_fade && curr_state_time_spent > img_display_time_s / 2) {
                    if(!load_next_image()) goto exit_error;
                }
            }
            SDL_Delay(100); //slow down but allow polling for events every 100ms
            break;

        case FADING: 
            curr_state_time_spent += ts; 
            float image_fade_value = curr_state_time_spent / img_fade_time_s;
            bool done_fading = false;
            if (image_fade_value >= 1.0f) { 
                image_fade_value = 1.0f;
                done_fading = true;
            }
            my_window.render(current_active_texture ? (1 - image_fade_value) : image_fade_value); 

            if (done_fading) {
                current_active_texture = !current_active_texture;
                curr_file_idx = back_texture_file_idx;
                new_image_loaded_since_fade = false;
                curr_state_time_spent = 0;
                curr_state = DISPLAY;
                if (!load_file_list(folder_path)) goto exit_error;
            }
            break;
        }
#ifdef DEBUG_RENDER
	    SDL_Log("%f FPS", 1.0f/ts);
#endif
    }

    loader_cleanup(); 
    return 0;

exit_error:
    loader_cleanup(); 
    return 1;
}
