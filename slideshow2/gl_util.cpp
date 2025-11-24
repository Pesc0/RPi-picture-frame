#include "gl_util.h"

#include <GLES2/gl2.h>
#include <string>

static GLint uFade;

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


void gl_init(int display_w, int display_h) {
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
    glClear(GL_COLOR_BUFFER_BIT);
}


void gl_render(float fade_amount) {
    glUniform1f(uFade, fade_amount);
    glClear(GL_COLOR_BUFFER_BIT); //could be omitted, but helps on vc4 apparently (not sure if it applies to brcm as well) https://docs.mesa3d.org/drivers/vc4.html
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}