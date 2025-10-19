#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengles2.h>


class SDL_GL_window {
public:
    SDL_GL_window();
    ~SDL_GL_window();

    void render(float fade_amount);
    bool wants_to_close();

private:
    SDL_Window* window;
    SDL_GLContext gl_context;
    SDL_WindowID ID;

    int display_w, display_h;
    GLint uFade;
};