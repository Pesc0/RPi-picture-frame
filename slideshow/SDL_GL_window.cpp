#include "SDL_GL_window.h"

#include <cstdlib> //exit()


GLuint compile_shader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, NULL, log);
        SDL_Log("Shader compile error: %s", log);
    }
    return shader;
}

GLuint create_program() {
    // Vertex shader
    const char* vertex_shader_src = R"(
        attribute vec2 aPos;
        attribute vec2 aTexCoord;
        varying vec2 vTexCoord;
        void main() {
            vTexCoord = aTexCoord;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    // Fragment shader
    const char* fragment_shader_src = R"(
        precision mediump float;
        varying vec2 vTexCoord;
        uniform sampler2D uTexture0;
        uniform sampler2D uTexture1;
        uniform float uFade; // 0.0 -> only texture0, 1.0 -> only texture1

        void main() {
            vec4 color1 = texture2D(uTexture0, vTexCoord);
            vec4 color2 = texture2D(uTexture1, vTexCoord);
            gl_FragColor = mix(color1, color2, uFade);
        }
    )";

    GLuint vert = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);

    GLint posAttrib = 0;    // location 0
    GLint texAttrib = 1;    // location 1
    glBindAttribLocation(program, posAttrib, "aPos"); 
    glBindAttribLocation(program, texAttrib, "aTexCoord"); 
     
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, 512, NULL, log);
        SDL_Log("Program link error: %s", log);
    }

    glDeleteShader(vert);
    glDeleteShader(frag);

    glEnableVertexAttribArray(posAttrib);
    glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
    
    glEnableVertexAttribArray(texAttrib);
    glVertexAttribPointer(texAttrib, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));

    return program;
}

GLuint create_texture(int w, int h) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Allocate empty texture initially 
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}



SDL_GL_window::SDL_GL_window() {
    // [If using SDL_MAIN_USE_CALLBACKS: all code below until the main loop starts would likely be your SDL_AppInit() function]
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Error: SDL_Init(): %s\n", SDL_GetError());
        exit(1);
    }

    // GL ES 2.0 
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    int num_displays;
    SDL_DisplayID *displays = SDL_GetDisplays(&num_displays);
    if (displays == nullptr)
    {
        SDL_Log("Error: SDL_GetDisplays(): %s\n", SDL_GetError());
        exit(1);
    }
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(displays[0]);
    if (mode == nullptr)
    {
        SDL_Log("Error: SDL_GetCurrentDisplayMode(): %s\n", SDL_GetError());
        exit(1);
    }
    display_w = mode->w; display_h = mode->h;
#ifdef DEBUG
    SDL_Log("Detected resolution: %dx%d", display_w, display_h);
#endif

    window = SDL_CreateWindow("", display_w, display_h, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS);
    if (window == nullptr)
    {
        SDL_Log("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        exit(1);
    }
    SDL_GetWindowSize(window, &display_w, &display_h);
    SDL_HideCursor();
    gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr)
    {
        SDL_Log("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        exit(1);
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync
    SDL_ShowWindow(window);
    ID = SDL_GetWindowID(window);

    GLfloat quadVertices[] = {
        // x, y, u, v
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };

    GLuint quadVBO;
    glGenBuffers(1, &quadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    GLuint shaderProgram = create_program();
    glUseProgram(shaderProgram);

    glViewport(0, 0, display_w, display_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.00f);

    glActiveTexture(GL_TEXTURE0);
    GLuint tex0 = create_texture(display_w, display_h);
    glBindTexture(GL_TEXTURE_2D, tex0);
    glUniform1i(glGetUniformLocation(shaderProgram, "uTexture0"), 0); //set uniform uTexture0 to use texture unit 0, which has tex0 bound

    glActiveTexture(GL_TEXTURE1);
    GLuint tex1 = create_texture(display_w, display_h);
    glBindTexture(GL_TEXTURE_2D, tex1);
    glUniform1i(glGetUniformLocation(shaderProgram, "uTexture1"), 1); //set uniform uTexture1 to use texture unit 1, which has tex1 bound

    uFade = glGetUniformLocation(shaderProgram, "uFade");
}

SDL_GL_window::~SDL_GL_window() {
    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void SDL_GL_window::render(float fade_amount) {
    glUniform1f(uFade, fade_amount);
    glClear(GL_COLOR_BUFFER_BIT); //could be omitted, but helps on vc4 apparently (not sure if it applies to brcm as well) https://docs.mesa3d.org/drivers/vc4.html
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    SDL_GL_SwapWindow(window);
}

SDL_WindowID SDL_GL_window::get_ID() {
    return this->ID;
}