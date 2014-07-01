#include <string.h>
#include <errno.h>

#include "vdpau_private.h"

const char *vertex_shader = "attribute vec4 vPosition;"
    "attribute vec2 aTexcoord;"
    "varying vec2 vTexcoord;"
    "void main(void) {"
    "   gl_Position = vPosition;"
    "   vTexcoord = aTexcoord;"
    "}";

static const char* fragment_shaders[] = {
    /* YUVI420 RGB */
    "precision mediump float;"
    "varying vec2 vTexcoord;"
    "uniform sampler2D s_ytex,s_utex,s_vtex;"
    "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
    "uniform vec3 rcoeff;"
    "uniform vec3 gcoeff;"
    "uniform vec3 bcoeff;"
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

    /* YUYV -> RGB */
    "precision mediump float;"
    "uniform sampler2D s_tex;"
    "varying vec2      vTexcoord;"
    "uniform float     stepX;"
    "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
    "uniform vec3 rcoeff;"
    "uniform vec3 gcoeff;"
    "uniform vec3 bcoeff;"
    "void main(void)"
    "{"
    "  float r,g,b;"
    "  vec2 pos    = vTexcoord;"
    "  pos         = vec2(pos.x - stepX * 0.25, pos.y);"
    "  float f     = fract(pos.x / stepX);"
    ""
    "  vec4 c1 = texture2D(s_tex, vec2(pos.x + (0.5 - f) * stepX, pos.y));"
    "  vec4 c2 = texture2D(s_tex, vec2(pos.x + (1.5 - f) * stepX, pos.y));"
    ""
    "  float leftY   = mix(c1.b, c1.r, f * 2.0);"
    "  float rightY  = mix(c1.r, c2.b, f * 2.0 - 1.0);"
    "  vec2  outUV   = mix(c1.ga, c2.ga, f);"
    ""
    "  float outY    = mix(leftY, rightY, step(0.5, f));"
    "  vec3  yuv     = vec3(outY, outUV);"
    "  "
    "  yuv += offset;"
    "  r = dot(yuv, rcoeff);"
    "  g = dot(yuv, gcoeff);"
    "  b = dot(yuv, bcoeff);"
    "  gl_FragColor=vec4(r,g,b,1.0);"
    "}",

    /* UYVY -> RGB */
    "precision mediump float;"
    "uniform sampler2D s_tex;"
    "varying vec2      vTexcoord;"
    "uniform float     stepX;"
    "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
    "uniform vec3 rcoeff;"
    "uniform vec3 gcoeff;"
    "uniform vec3 bcoeff;"
    "void main(void)"
    "{"
    "  float r,g,b;"
    "  vec2 pos    = vTexcoord;"
    "  pos         = vec2(pos.x - stepX * 0.25, pos.y);"
    "  float f     = fract(pos.x / stepX);"
    ""
    "  vec4 c1 = texture2D(s_tex, vec2(pos.x + (0.5 - f) * stepX, pos.y));"
    "  vec4 c2 = texture2D(s_tex, vec2(pos.x + (1.5 - f) * stepX, pos.y));"
    ""
    "  float leftY   = mix(c1.g, c1.a, f * 2.0);"
    "  float rightY  = mix(c1.a, c2.g, f * 2.0 - 1.0);"
    "  vec2  outUV   = mix(c1.br, c2.br, f);"
    ""
    "  float outY    = mix(leftY, rightY, step(0.5, f));"
    "  vec3  yuv     = vec3(outY, outUV);"
    "  "
    "  yuv += offset;"
    "  r = dot(yuv, rcoeff);"
    "  g = dot(yuv, gcoeff);"
    "  b = dot(yuv, bcoeff);"
    "  gl_FragColor=vec4(r,g,b,1.0);"
    "}",

    /* NV12/NV21 to RGB conversion */
    "precision mediump float;"
    "varying vec2 vTexcoord;"
    "uniform sampler2D s_ytex,s_uvtex;"
    "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
    "uniform vec3 rcoeff;"
    "uniform vec3 gcoeff;"
    "uniform vec3 bcoeff;"
    "void main(void) {"
    "  float r,g,b;"
    "  vec3 yuv;"
    "  yuv.x=texture2D(s_ytex,vTexcoord).r;"
    "  yuv.yz=texture2D(s_uvtex,vTexcoord).ra;"
    "  yuv += offset;"
    "  r = dot(yuv, rcoeff);"
    "  g = dot(yuv, gcoeff);"
    "  b = dot(yuv, bcoeff);"
    "  gl_FragColor=vec4(r,g,b,1.0);"
    "}",

    /* YUV8 to RGB conversion */
    "precision mediump float;"
    "varying vec2 vTexcoord;"
    "uniform sampler2D s_tex;"
    "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
    "uniform vec3 rcoeff;"
    "uniform vec3 gcoeff;"
    "uniform vec3 bcoeff;"
    "void main(void) {"
    "  float r,g,b;"
    "  vec3 yuv;"
    "  yuv.xyz=texture2D(s_tex,vTexcoord).rgb;"
    "  yuv += offset;"
    "  r = dot(yuv, rcoeff);"
    "  g = dot(yuv, gcoeff);"
    "  b = dot(yuv, bcoeff);"
    "  gl_FragColor=vec4(r,g,b,1.0);"
    "}",

    /* VUY8 to RGB conversion */
    "precision mediump float;"
    "varying vec2 vTexcoord;"
    "uniform sampler2D s_tex;"
    "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
    "uniform vec3 rcoeff;"
    "uniform vec3 gcoeff;"
    "uniform vec3 bcoeff;"
    "void main(void) {"
    "  float r,g,b;"
    "  vec3 yuv;"
    "  yuv.xyz=texture2D(s_tex,vTexcoord).bgr;"
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
    "   gl_FragColor = texture2D(s_tex, vTexcoord);"
    "}",

    /* BRSWAP_COPY */
    "precision mediump float;"
    "varying vec2 vTexcoord;"
    "uniform sampler2D s_tex;"
    "void main(void) {"
    "   gl_FragColor = texture2D(s_tex, vTexcoord).bgra;"
    "}"

};

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
gl_load_shaders (shader_ctx_t *shader,
                 shader_type_t process_type)
{
    shader->vertex_shader = gl_load_shader (vertex_shader,
                                          GL_VERTEX_SHADER);
    if (!shader->vertex_shader)
        return -EINVAL;

    shader->fragment_shader = gl_load_shader (fragment_shaders[process_type],
                                            GL_FRAGMENT_SHADER);
    if (!shader->fragment_shader)
        return -EINVAL;

    return 0;
}

int
gl_init_shader (shader_ctx_t *shader,
                shader_type_t process_type)
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
    shader->rcoeff_loc = glGetAttribLocation(shader->program, "rcoeff");
    shader->gcoeff_loc = glGetAttribLocation(shader->program, "gcoeff");
    shader->bcoeff_loc = glGetAttribLocation(shader->program, "bcoeff");

    switch(process_type) {
        case SHADER_YUVI420_RGB:
            shader->texture[0] = glGetUniformLocation(shader->program, "s_ytex");
            CHECKEGL
            shader->texture[1] = glGetUniformLocation(shader->program, "s_utex");
            CHECKEGL
            shader->texture[2] = glGetUniformLocation(shader->program, "s_vtex");
            CHECKEGL
            break;
        case SHADER_YUVNV12_RGB:
            shader->texture[0] = glGetUniformLocation(shader->program, "s_ytex");
            CHECKEGL
            shader->texture[1] = glGetUniformLocation(shader->program, "s_uvtex");
            CHECKEGL
            break;
        case SHADER_YUYV422_RGB:
        case SHADER_UYVY422_RGB:
            shader->texture[0] = glGetUniformLocation(shader->program, "s_tex");
            CHECKEGL
            shader->stepX = glGetUniformLocation(shader->program, "stepX");
            CHECKEGL
            break;
        case SHADER_YUV8444_RGB:
        case SHADER_VUY8444_RGB:
        case SHADER_COPY:
        case SHADER_BRSWAP_COPY:
            shader->texture[0] = glGetUniformLocation(shader->program, "s_tex");
            CHECKEGL
            break;
    }
    return 0;
}

void
gl_delete_shader(shader_ctx_t *shader)
{
    glDeleteShader (shader->vertex_shader);
    shader->vertex_shader = 0;

    glDeleteShader (shader->fragment_shader);
    shader->fragment_shader = 0;

    glDeleteProgram (shader->program);
    shader->program = 0;
}

GLuint
gl_create_texture(GLuint tex_filter)
{
    GLuint tex_id = 0;

    glGenTextures (1, &tex_id);
    if (!tex_id) {
        VDPAU_DBG ("Could not create texture");
        return 0;
    }
    glBindTexture (GL_TEXTURE_2D, tex_id);
    CHECKEGL

    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, tex_filter);
    CHECKEGL
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, tex_filter);
    CHECKEGL

    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    CHECKEGL
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CHECKEGL

    return tex_id;
}
