
#include "load_image.h"
#include "gl_util.h"
#include "drm_util.h"
#include "gbm_util.h"
#include "egl_util.h"

#include <math.h>
#include <string>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>
#include <cassert>
#include <cstring>

#define DEFAULT_IMG_DISPLAY_TIME 5.0f 
#define DEFAULT_IMG_FADE_TIME 0.5f
#define DEFAULT_IMG_FOLDER_PATH "/tmp"
#define DEFAULT_GPIO_LINE 23  // GPIO23

std::atomic<bool> stop_requested(false);
using my_clock = std::chrono::high_resolution_clock;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        stop_requested = true;  
    }
}



int main(int, char**)
{
    enum State { DISPLAY, FADING };
    State curr_state = DISPLAY;
    float curr_state_time_spent = 0.0f;
    bool paused = false;

    std::signal(SIGINT, signal_handler);

    const char* env_img_display_time = getenv("IMG_DISPLAY_TIME");
    const float img_display_time_s = env_img_display_time != nullptr ? std::stof(env_img_display_time) : DEFAULT_IMG_DISPLAY_TIME;

    const char* env_img_fade_time = getenv("IMG_FADE_TIME");
    const float img_fade_time_s = env_img_fade_time != nullptr ? std::stof(env_img_fade_time) : DEFAULT_IMG_FADE_TIME;

    const char* env_folder_path = getenv("IMG_FOLDER_PATH");
    const std::string folder_path = env_folder_path != nullptr ? env_folder_path : DEFAULT_IMG_FOLDER_PATH;

    const char* env_led = getenv("LED_PAUSE_INDICATOR_GPIO");
    unsigned int led_pin = env_led != nullptr ? (unsigned int)std::stoul(env_led) : DEFAULT_GPIO_LINE;


    DRM drm;
	GBM gbm(drm);
	EGL egl(gbm);
    GL gl(drm, gbm, egl);

    ImageLoader my_loader(folder_path);
    if (!my_loader.init_is_successful()) return 1;

    //FIXME
    // first render, twice because of a bug on some opengl implementations where 
    // one render would show a black screen, but it would fix itself with a second one.
    //my_window.render(0.0f);
    //my_window.render(0.0f);

    auto prevTime = my_clock::now();    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    while (!stop_requested) // Main loop
    {
        auto crntTime = my_clock::now();        
        std::chrono::duration<float> delta = crntTime - prevTime;
        float ts = delta.count();
        prevTime = crntTime;

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
                else if (!my_loader.new_image_has_been_loaded() && curr_state_time_spent > img_display_time_s / 2) {
                    if(!my_loader.load_next_image()) return 1;
                }
            }
            //slow down but allow polling for events every 100ms
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            break;

        case FADING: 
            curr_state_time_spent += ts; 
            float image_fade_value = curr_state_time_spent / img_fade_time_s;
            bool done_fading = false;
            if (image_fade_value >= 1.0f) { 
                image_fade_value = 1.0f;
                done_fading = true;
            }

            gl.gl_render(my_loader.correct_fade_direction(image_fade_value));

            if (done_fading) {
                my_loader.switch_active_texture();
                if (!my_loader.load_file_list()) return 1;
                curr_state_time_spent = 0;
                curr_state = DISPLAY;
            }
            break;
        }
#ifdef DEBUG_RENDER
	    printf("%f FPS", 1.0f/ts);
#endif


    }

    return 0;
}
