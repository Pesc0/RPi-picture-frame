
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengles2.h>
#include <stb_image.h>

#include <math.h>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
//#include <cstdlib> 


//#define DEBUG
//#define DEBUG_RENDER

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

int display_w, display_h;
GLint uFade;


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

GLuint create_texture() {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Allocate empty texture initially 
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, display_w, display_h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

bool load_image(const std::string& path, GLenum texture_unit) {
    int width, height, channels;
#ifdef DEBUG
    Uint64 begin = SDL_GetPerformanceCounter(); 
#endif
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb);
    if (!data) {
        SDL_Log("Failed to load image %s: %s", path.c_str(), stbi_failure_reason());
        broken_files.push_back(path);
        return false;
    }
#ifdef DEBUG
    Uint64 read = SDL_GetPerformanceCounter(); 
#endif
    glActiveTexture(texture_unit); // bind texture unit, texture is already bound inside it
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data); //glTexSubImage2D does not work on RPi
#ifdef DEBUG
    Uint64 uploaded = SDL_GetPerformanceCounter(); 
#endif
    stbi_image_free(data);
#ifdef DEBUG
    Uint64 end = SDL_GetPerformanceCounter(); 
    float read_time = (read - begin) / 1000000000.0f; //nanoseconds to seconds
    float upload_time = (uploaded - read) / 1000000000.0f; //nanoseconds to seconds
    float total_time = (end - begin) / 1000000000.0f; //nanoseconds to seconds
    SDL_Log("Loaded %s in %fs, uploaded to GPU in %fs, total: %fs", path.c_str(), read_time, upload_time, total_time);
#endif
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

void render(SDL_Window* window, float fade_amount) {
    glUniform1f(uFade, fade_amount);
    glClear(GL_COLOR_BUFFER_BIT); //could be omitted, but helps on vc4 apparently (not sure if it applies to brcm as well) https://docs.mesa3d.org/drivers/vc4.html
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    SDL_GL_SwapWindow(window);
}

int main(int, char**)
{
    const char* env_img_display_time = getenv("IMG_DISPLAY_TIME");
    const float img_display_time_s = env_img_display_time != nullptr ? std::stof(env_img_display_time) : DEFAULT_IMG_DISPLAY_TIME;

    const char* env_img_fade_time = getenv("IMG_FADE_TIME");
    const float img_fade_time_s = env_img_fade_time != nullptr ? std::stof(env_img_fade_time) : DEFAULT_IMG_FADE_TIME;

    const char* env_folder_path = getenv("IMG_FOLDER_PATH");
    const std::string folder_path = env_folder_path != nullptr ? env_folder_path : DEFAULT_IMG_FOLDER_PATH;

    // [If using SDL_MAIN_USE_CALLBACKS: all code below until the main loop starts would likely be your SDL_AppInit() function]
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Error: SDL_Init(): %s\n", SDL_GetError());
        return 1;
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
        return 1;
    }
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(displays[0]);
    if (mode == nullptr)
    {
        SDL_Log("Error: SDL_GetCurrentDisplayMode(): %s\n", SDL_GetError());
        return 1;
    }
    display_w = mode->w; display_h = mode->h;
#ifdef DEBUG
    SDL_Log("Detected resolution: %dx%d", display_w, display_h);
#endif

    SDL_Window* window = SDL_CreateWindow("", display_w, display_h, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS);
    if (window == nullptr)
    {
        SDL_Log("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return 1;
    }
    SDL_GetWindowSize(window, &display_w, &display_h);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr)
    {
        SDL_Log("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync
    SDL_ShowWindow(window);

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
    GLuint tex0 = create_texture();
    glBindTexture(GL_TEXTURE_2D, tex0);
    glUniform1i(glGetUniformLocation(shaderProgram, "uTexture0"), 0); //set uniform uTexture0 to use texture unit 0, which has tex0 bound

    glActiveTexture(GL_TEXTURE1);
    GLuint tex1 = create_texture();
    glBindTexture(GL_TEXTURE_2D, tex1);
    glUniform1i(glGetUniformLocation(shaderProgram, "uTexture1"), 1); //set uniform uTexture1 to use texture unit 1, which has tex1 bound

    uFade = glGetUniformLocation(shaderProgram, "uFade");

    stbi_set_flip_vertically_on_load(1);

    int event_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (event_fd < 0) { perror("open"); return 1; }

    // first render
    if (!load_file_list(folder_path)) return 1;
    if (!load_image(files[curr_file_idx], GL_TEXTURE0)) return 1; //load initial image on texture zero
    render(window, 0.0f);
    Uint64 prevTime = SDL_GetPerformanceCounter(); 
    SDL_Delay(100);

    // Main loop
    bool done = false;
    while (!done)
    {
        Uint64 crntTime = SDL_GetPerformanceCounter();
        float ts = (crntTime - prevTime) / 1000000000.0f; //nanoseconds to seconds
        prevTime = crntTime;

        // [If using SDL_MAIN_USE_CALLBACKS: call ImGui_ImplSDL3_ProcessEvent() from your SDL_AppEvent() function]
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID != SDL_GetWindowID(window))
                    break;
            case SDL_EVENT_QUIT:
                done = true;
                break;
            }
        }

        struct input_event ev;
        while (read(event_fd, &ev, sizeof(ev)) > 0) {
            if (ev.type == EV_KEY && ev.value == 1) {
                switch (ev.code)
                {
                case 57: //space
                    paused = !paused;
                    break; 

                case 105: //left = prev
                    if (curr_state == FADING) break;
                    if(!load_prev_image()) return 1;
                    curr_state_time_spent = img_fade_time_s; //jump directly to next image, don't fade
                    curr_state = FADING;
                    break;

                case 106: //right = next
                    if (curr_state == FADING) break;
                    if(!load_next_image()) return 1;
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
                    if(!load_next_image()) return 1;
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
            render(window, current_active_texture ? (1 - image_fade_value) : image_fade_value); 

            if (done_fading) {
                current_active_texture = !current_active_texture;
                curr_file_idx = back_texture_file_idx;
                new_image_loaded_since_fade = false;
                curr_state_time_spent = 0;
                curr_state = DISPLAY;
                if (!load_file_list(folder_path)) return 1;
            }
            break;
        }
#ifdef DEBUG_RENDER
	    SDL_Log("%f FPS", 1.0f/ts);
#endif
    }

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
