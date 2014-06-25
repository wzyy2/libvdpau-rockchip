#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <GLES2/gl2.h>

#include "vdpau_private.h"

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

