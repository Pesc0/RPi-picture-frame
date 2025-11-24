
#include "load_image.h"
#include "drm.h"


#include <math.h>
#include <string>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>

#define DEFAULT_IMG_DISPLAY_TIME 60.0f 
#define DEFAULT_IMG_FADE_TIME 0.5f
#define DEFAULT_IMG_FOLDER_PATH "/tmp"
#define DEFAULT_GPIO_LINE 23  // GPIO23

std::atomic<bool> stop_requested(false);
using clock = std::chrono::high_resolution_clock;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        stop_requested = true;  
    }
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
        printf("Shader compile error: %s", log);
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
        printf("Program link error: %s", log);
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


    char mode_str[DRM_DISPLAY_MODE_LEN] = "";

	static const struct drm *drm = init_drm_atomic(NULL, mode_str, -1, 0, ~0u, false);
	if (!drm) {
		printf("failed to initialize DRM\n");
		return -1;
	}

	static const struct gbm *gbm = init_gbm(drm->fd, drm->mode->hdisplay, drm->mode->vdisplay, DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR, false);
	if (!gbm) {
		printf("failed to initialize GBM\n");
		return -1;
	}

	static const struct egl *egl = init_egl(gbm, 0);
	if (!egl) {
		printf("failed to initialize EGL\n");
		return -1;
	}

    int display_w = gbm->width, display_h = gbm->height;
#ifdef DEBUG
    printf("Detected resolution: %dx%d", display_w, display_h);
#endif

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

    GLint uFade = glGetUniformLocation(shaderProgram, "uFade");

    if (egl_check(egl, eglDupNativeFenceFDANDROID) ||
	    egl_check(egl, eglCreateSyncKHR) ||
	    egl_check(egl, eglDestroySyncKHR) ||
	    egl_check(egl, eglWaitSyncKHR) ||
	    egl_check(egl, eglClientWaitSyncKHR))
		return -1;

    ImageLoader my_loader(folder_path);
    if (!my_loader.init_is_successful()) return 1;

    //FIXME
    // first render, twice because of a bug on some opengl implementations where 
    // one render would show a black screen, but it would fix itself with a second one.
    //my_window.render(0.0f);
    //my_window.render(0.0f);

    auto prevTime = clock::now();    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    struct gbm_bo *bo = NULL;
	struct drm_fb *fb;
	uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_ATOMIC_ALLOW_MODESET;

    while (!stop_requested) // Main loop
    {
        auto crntTime = clock::now();        
        std::chrono::duration<float> delta = crntTime - prevTime;
        float ts = delta.count();
        prevTime = crntTime;

		struct gbm_bo *next_bo;
		EGLSyncKHR gpu_fence = NULL;   /* out-fence from gpu, in-fence to kms */
		EGLSyncKHR kms_fence = NULL;   /* in-fence to gpu, out-fence from kms */

        if (drm.kms_out_fence_fd != -1) {
			kms_fence = create_fence(egl, drm.kms_out_fence_fd);
			assert(kms_fence);

			/* driver now has ownership of the fence fd: */
			drm.kms_out_fence_fd = -1;

			/* wait "on the gpu" (ie. this won't necessarily block, but
			 * will block the rendering until fence is signaled), until
			 * the previous pageflip completes so we don't render into
			 * the buffer that is still on screen.
			 */
			egl->eglWaitSyncKHR(egl->display, kms_fence, 0);
		}

		if (!gbm->surface) {
			glBindFramebuffer(GL_FRAMEBUFFER, egl->fbs[frame % NUM_BUFFERS].fb);
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

            glUniform1f(uFade, my_loader.correct_fade_direction(image_fade_value));
            glClear(GL_COLOR_BUFFER_BIT); //could be omitted, but helps on vc4 apparently (not sure if it applies to brcm as well) https://docs.mesa3d.org/drivers/vc4.html
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

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

		/* insert fence to be singled in cmdstream.. this fence will be
		 * signaled when gpu rendering done
		 */
		gpu_fence = create_fence(egl, EGL_NO_NATIVE_FENCE_FD_ANDROID);
		assert(gpu_fence);

		if (gbm->surface) {
			eglSwapBuffers(egl->display, egl->surface);
		}

		/* after swapbuffers, gpu_fence should be flushed, so safe
		 * to get fd:
		 */
		drm.kms_in_fence_fd = egl->eglDupNativeFenceFDANDROID(egl->display, gpu_fence);
		egl->eglDestroySyncKHR(egl->display, gpu_fence);
		assert(drm.kms_in_fence_fd != -1);

		if (gbm->surface) {
			next_bo = gbm_surface_lock_front_buffer(gbm->surface);
		} else {
			next_bo = gbm->bos[frame % NUM_BUFFERS];
		}
		if (!next_bo) {
			printf("Failed to lock frontbuffer\n");
			return -1;
		}
		fb = drm_fb_get_from_bo(next_bo);
		if (!fb) {
			printf("Failed to get a new framebuffer BO\n");
			return -1;
		}

		if (kms_fence) {
			EGLint status;

			/* Wait on the CPU side for the _previous_ commit to
			 * complete before we post the flip through KMS, as
			 * atomic will reject the commit if we post a new one
			 * whilst the previous one is still pending.
			 */
			do {
				status = egl->eglClientWaitSyncKHR(egl->display,
								   kms_fence,
								   0,
								   EGL_FOREVER_KHR);
			} while (status != EGL_CONDITION_SATISFIED_KHR);

			egl->eglDestroySyncKHR(egl->display, kms_fence);
		}

		if (!drm.nonblocking) {
			/* Check for user input: */
			struct pollfd fdset[] = { {
				.fd = STDIN_FILENO,
				.events = POLLIN,
			} };
			ret = poll(fdset, ARRAY_SIZE(fdset), 0);
			if (ret > 0) {
				printf("user interrupted!\n");
				return 0;
			}
		}

		/*
		 * Here you could also update drm plane layers if you want
		 * hw composition
		 */
		ret = drm_atomic_commit(fb->fb_id, flags);
		if (ret) {
			printf("failed to commit: %s\n", strerror(errno));
			return -1;
		}

		/* release last buffer to render on again: */
		if (bo && gbm->surface)
			gbm_surface_release_buffer(gbm->surface, bo);
		bo = next_bo;

		/* Allow a modeset change for the first commit only. */
		flags &= ~(DRM_MODE_ATOMIC_ALLOW_MODESET);
    }

    return 0;
}
