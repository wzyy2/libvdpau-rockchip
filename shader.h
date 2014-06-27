#ifndef _SHADER_H__
#define _SHADER_H__

typedef enum
{
    SHADER_YUVI420_RGB = 0,
    SHADER_COPY,
    SHADER_BRSWAP_COPY
} GLESShaderTypes;

typedef struct 
{
    GLuint program;
    GLuint vertex_shader;
    GLuint fragment_shader;

    /* standard locations, used in most shaders */
    GLint position_loc;
    GLint texcoord_loc;
} GLESShader;


/* initialises the GL program with its shaders and sets the program handle
 * returns 0 on succes, -1 on failure*/
int
gl_init_shader (GLESShader *shader,
                GLESShaderTypes process_type);
void
gl_delete_shader (GLESShader *shader);
#endif
