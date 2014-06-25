#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <GLES2/gl2.h>

#include "vdpau_private.h"

static const char* shaders[] = {
    /* YUV I420 -> RGB */
	"precision mediump float;"
	"varying vec2 vTexcoord;"
	"uniform sampler2D s_ytex,s_utex,s_vtex;"
	"const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
	"const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
	"const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
	"const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
	"void main(void) {"
	"  float r,g,b;"
	"  vec3 yuv;"
	"  yuv.x=texture2D(s_ytex,vTexcoord).r;"
	"  yuv.y=texture2D(s_utex,vTexcoord).r;"
	"  yuv.z=texture2D(s_vtex,vTexcoord).r;"
	"  yuv += offset;"
	"  r = dot(yuv, rcoeff);"
	"  g = dot(yuv, gcoeff);"
	"  b = dot(yuv, bcoeff);"
	"  gl_FragColor=vec4(r,g,b,1.0);"
	"}",
	
	/* COPY */
    "precision mediump float;"
	"varying vec2 vTexcoord;"
	"uniform sampler2D s_tex;"
	"void main(void) {"
	"	gl_FragColor = texture2D(s_tex, vTexcoord);"
	"}"
};

#define VERTEX_SHADER \
	"attribute vec4 vPosition;"\
	"attribute vec2 aTexcoord;"\
	"varying vec2 vTexcoord;"\
	"void main(void) {"\
	"   gl_Position = vPosition;"\
	"   vTexcoord = aTexcoord;"\
	"}"

/* load and compile a shader src into a shader program */
static GLuint
gl_load_shader (const char *shader_src,
                       GLenum type)
{
    GLuint shader = 0;
    GLint compiled;
    size_t src_len;

    /* create a shader object */
    shader = glCreateShader (type);
    if (shader == 0) {
        VDPAU_DBG ("Could not create shader object");
        return 0;
    }

    /* load source into shader object */
    src_len = strlen (shader_src);
    glShaderSource (shader, 1, (const GLchar**) &shader_src,
                    (const GLint*) &src_len);

    /* compile the shader */
    glCompileShader (shader);

    /* check compiler status */
    glGetShaderiv (shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;

        glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &info_len);
        if(info_len > 1) {
            char *info_log = malloc (sizeof(char) * info_len);
            glGetShaderInfoLog (shader, info_len, NULL, info_log);

            VDPAU_DBG ("Failed to compile shader: %s", info_log);
            free (info_log);
        }

        glDeleteShader (shader);
        shader = 0;
	} else {
		VDPAU_DBG ("Shader compiled succesfully");
    }

    return shader;
}


/*
 * Load vertex and fragment Shaders.
 * Vertex shader is a predefined default, fragment shader can be configured
 * through process_type */
static int
gl_load_shaders (GLESShader *shader,
                 GLESShaderTypes process_type)
{
    shader->vertex_shader = gl_load_shader (VERTEX_SHADER,
                                          GL_VERTEX_SHADER);
    if (!shader->vertex_shader)
        return -EINVAL;

    shader->fragment_shader = gl_load_shader (shaders[process_type],
                                            GL_FRAGMENT_SHADER);
    if (!shader->fragment_shader)
        return -EINVAL;

    return 0;
}

int
gl_init_shader (GLESShader *shader,
                GLESShaderTypes process_type)
{
    int linked;
    GLint err;
    int ret;

    shader->program = glCreateProgram();
    if(!shader->program) {
        VDPAU_DBG("Could not create GL program");
        return -ENOMEM;
    }

    /* load the shaders */
    ret = gl_load_shaders(shader, process_type);
    if(ret < 0) {
        VDPAU_DBG("Could not create GL shaders: %d", ret);
        return ret;
    }

    glAttachShader(shader->program, shader->vertex_shader);
    err = glGetError ();
    if (err != GL_NO_ERROR) {
        VDPAU_DBG ("Error while attaching the vertex shader: 0x%04x", err);
    }

    glAttachShader(shader->program, shader->fragment_shader);
    err = glGetError ();
    if (err != GL_NO_ERROR) {
        VDPAU_DBG ("Error while attaching the fragment shader: 0x%04x", err);
    }

    glBindAttribLocation(shader->program, 0, "vPosition");
    glLinkProgram(shader->program);

    /* check linker status */
    glGetProgramiv(shader->program, GL_LINK_STATUS, &linked);
    if(!linked) {
        GLint info_len = 0;
        VDPAU_DBG("Linker failure");

        glGetProgramiv(shader->program, GL_INFO_LOG_LENGTH, &info_len);
        if(info_len > 1) {
            char *info_log = malloc(sizeof(char) * info_len);
            glGetProgramInfoLog(shader->program, info_len, NULL, info_log);

            VDPAU_DBG("Failed to link GL program: %s", info_log);
            free(info_log);
        }

        glDeleteProgram(shader->program);
        return -EINVAL;
    }

    glUseProgram(shader->program);

    shader->position_loc = glGetAttribLocation(shader->program, "vPosition");
    shader->texcoord_loc = glGetAttribLocation(shader->program, "aTexcoord");

    return 0;
}

void
gl_delete_shader(GLESShader *shader)
{
    glDeleteShader (shader->vertex_shader);
    shader->vertex_shader = 0;

    glDeleteShader (shader->fragment_shader);
    shader->fragment_shader = 0;

    glDeleteProgram (shader->program);
    shader->program = 0;
}
