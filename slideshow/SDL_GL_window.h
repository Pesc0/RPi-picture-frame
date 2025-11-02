#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengles2.h>
#include <gpiod.h>

#define GPIO_CHIP_NAME "/dev/gpiochip0"
#define GPIO_LINE 21  // GPIO21


class SDL_GL_window {
public:
    SDL_GL_window();
    ~SDL_GL_window();

    void render(float fade_amount);
    SDL_WindowID get_ID();
    void set_led(bool state);

private:
    SDL_Window* window;
    SDL_GLContext gl_context;
    SDL_WindowID ID;

    int display_w, display_h;
    GLint uFade;

    bool have_led;
    struct gpiod_chip *gpio_chip;
    struct gpiod_line_settings *settings;
    struct gpiod_line_config *line_cfg;
    struct gpiod_request_config *req_cfg;
    struct gpiod_line_request *request;
};