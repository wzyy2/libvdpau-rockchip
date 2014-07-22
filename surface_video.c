/*
 * Copyright (c) 2013 Jens Kuske <jenskuske@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "vdpau_private.h"

VdpStatus vdp_video_surface_create(VdpDevice device,
                                   VdpChromaType chroma_type,
                                   uint32_t width,
                                   uint32_t height,
                                   VdpVideoSurface *surface)
{
    if (!surface)
        return VDP_STATUS_INVALID_POINTER;

    if (!width || !height)
        return VDP_STATUS_INVALID_SIZE;

    device_ctx_t *dev = handle_get(device);
    if (!dev)
        return VDP_STATUS_INVALID_HANDLE;

    video_surface_ctx_t *vs = calloc(1, sizeof(video_surface_ctx_t));
    if (!vs)
        return VDP_STATUS_RESOURCES;

    vs->device = dev;
    vs->width = width;
    vs->height = height;
    vs->chroma_type = chroma_type;

    switch (chroma_type)
    {
    case VDP_CHROMA_TYPE_420:
        break;
    default:
        free(vs);
        return VDP_STATUS_INVALID_CHROMA_TYPE;
    }

    VDPAU_DBG ("egl make context current");
    if (!eglMakeCurrent(dev->egl.display, dev->egl.surface,
                        dev->egl.surface, dev->egl.context)) {
        VDPAU_DBG ("Could not set EGL context to current %x", eglGetError());
        return VDP_STATUS_RESOURCES;
    }

    vs->y_tex = gl_create_texture(GL_NEAREST);
    vs->u_tex = gl_create_texture(GL_NEAREST);
    vs->v_tex = gl_create_texture(GL_NEAREST);
    vs->rgb_tex = gl_create_texture(GL_LINEAR);

    glGenFramebuffers (1, &vs->framebuffer);
    CHECKEGL

    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                  GL_UNSIGNED_BYTE, NULL);
    CHECKEGL

    glBindFramebuffer (GL_FRAMEBUFFER, vs->framebuffer);
    CHECKEGL

    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, vs->rgb_tex, 0);
    CHECKEGL

    if (!eglMakeCurrent(dev->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
        VDPAU_DBG ("Could not set EGL context to none %x", eglGetError());
        return VDP_STATUS_RESOURCES;
    }

    int handle = handle_create(vs);
    if (handle == -1)
    {
        free(vs);
        return VDP_STATUS_RESOURCES;
    }

    *surface = handle;

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_destroy(VdpVideoSurface surface)
{
    video_surface_ctx_t *vs = handle_get(surface);
    if (!vs)
        return VDP_STATUS_INVALID_HANDLE;

    const GLuint framebuffers[] = {
        vs->framebuffer
    };

    const GLuint textures[] = {
        vs->y_tex,
        vs->u_tex,
        vs->v_tex,
        vs->rgb_tex
    };

    glDeleteFramebuffers (1, framebuffers);
    glDeleteTextures (4, textures);

    handle_destroy(surface);
    free(vs);

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_get_parameters(VdpVideoSurface surface,
                                           VdpChromaType *chroma_type,
                                           uint32_t *width,
                                           uint32_t *height)
{
    video_surface_ctx_t *vid = handle_get(surface);
    if (!vid)
        return VDP_STATUS_INVALID_HANDLE;

    if (chroma_type)
        *chroma_type = vid->chroma_type;

    if (width)
        *width = vid->width;

    if (height)
        *height = vid->height;

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_get_bits_y_cb_cr(VdpVideoSurface surface,
                                             VdpYCbCrFormat destination_ycbcr_format,
                                             void *const *destination_data,
                                             uint32_t const *destination_pitches)
{
    video_surface_ctx_t *vs = handle_get(surface);
    if (!vs)
        return VDP_STATUS_INVALID_HANDLE;

    return VDP_STATUS_ERROR;
}

static GLfloat vVertices[] =
{
    -1.0f, -1.0f,
    0.0f, 1.0f,

    1.0f, -1.0f,
    1.0f, 1.0f,

    1.0f, 1.0f,
    1.0f, 0.0f,

    -1.0f, 1.0f,
    0.0f, 0.0f,
};
static GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

// BT.601, which is the standard for SDTV.
static const GLfloat kColorConversion601[3][3] = {
    {1.164,  0.0,    1.596},
    {1.164, -0.392, -0.813},
    {1.164,  2.017,  0.0}
};

// BT.709, which is the standard for HDTV.
static const GLfloat kColorConversion709[3][3] = {
    {1.164,  0.0,    1.793},
    {1.164, -0.213, -0.533},
    {1.164,  2.112,  0.0}
};

static void shader_init(video_surface_ctx_t *vs, shader_ctx_t *shader)
{
    glBindFramebuffer (GL_FRAMEBUFFER, vs->framebuffer);
    CHECKEGL

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER) ;
    if(status != GL_FRAMEBUFFER_COMPLETE) {
        VDPAU_DBG("failed to make complete framebuffer object %x", status);
    }

    glUseProgram (shader->program);
    CHECKEGL

    glViewport(0, 0, vs->width, vs->height);
    CHECKEGL

    glClear (GL_COLOR_BUFFER_BIT);
    CHECKEGL

    glVertexAttribPointer (shader->position_loc, 2,
                           GL_FLOAT, GL_FALSE, 4 * sizeof (GLfloat),
                           vVertices);
    CHECKEGL
    glEnableVertexAttribArray (shader->position_loc);
    CHECKEGL

    glVertexAttribPointer (shader->texcoord_loc, 2,
                           GL_FLOAT, GL_FALSE, 4 * sizeof (GLfloat),
                           &vVertices[2]);
    CHECKEGL
    glEnableVertexAttribArray (shader->texcoord_loc);
    CHECKEGL

    if(vs->height > 576) {
        glUniform3fv(shader->rcoeff_loc, 1, kColorConversion709[0]);
        CHECKEGL
        glUniform3fv(shader->gcoeff_loc, 1, kColorConversion709[1]);
        CHECKEGL
        glUniform3fv(shader->bcoeff_loc, 1, kColorConversion709[2]);
        CHECKEGL
    } else {
        glUniform3fv(shader->rcoeff_loc, 1, kColorConversion601[0]);
        CHECKEGL
        glUniform3fv(shader->gcoeff_loc, 1, kColorConversion601[1]);
        CHECKEGL
        glUniform3fv(shader->bcoeff_loc, 1, kColorConversion601[2]);
        CHECKEGL
    }
    CHECKEGL
}

static void shader_draw(video_surface_ctx_t *vs)
{
    glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    CHECKEGL

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

VdpStatus vdp_video_surface_put_bits_y_cb_cr(VdpVideoSurface surface,
                                             VdpYCbCrFormat source_ycbcr_format,
                                             void const *const *source_data,
                                             uint32_t const *source_pitches)
{
    shader_ctx_t *shader;
    video_surface_ctx_t *vs = handle_get(surface);
    if (!vs)
        return VDP_STATUS_INVALID_HANDLE;

    vs->source_format = source_ycbcr_format;

    device_ctx_t *dev = vs->device;
    if (!eglMakeCurrent(dev->egl.display, dev->egl.surface,
                        dev->egl.surface, dev->egl.context)) {
        VDPAU_ERR("Could not set EGL context to current %x", eglGetError());
        return VDP_STATUS_ERROR;
    }

    switch (source_ycbcr_format)
    {
    case VDP_YCBCR_FORMAT_YUYV:
    case VDP_YCBCR_FORMAT_UYVY:
        if (vs->chroma_type != VDP_CHROMA_TYPE_422)
            goto chroma;

        VDPAU_DBG("YUYV");
        if (vs->width != source_pitches[0]) {
            VDPAU_DBG("YUYV %d, %d", vs->width, source_pitches[0]);
        }

        if (source_ycbcr_format == VDP_YCBCR_FORMAT_YUYV)
            shader = &vs->device->egl.yuyv422_rgb;
        else
            shader = &vs->device->egl.uyvy422_rgb;

        shader_init(vs, shader);

        /* yuv component */
        glActiveTexture(GL_TEXTURE0);
        CHECKEGL
        glBindTexture (GL_TEXTURE_2D, vs->y_tex);
        CHECKEGL
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vs->width/2,
                     vs->height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, source_data[0]);
        CHECKEGL
        glUniform1i (shader->texture[0], 0);
        CHECKEGL

        glUniform1f (shader->stepX, 1.0f / vs->width);
        CHECKEGL

        shader_draw(vs);
        break;

    case VDP_YCBCR_FORMAT_Y8U8V8A8:
    case VDP_YCBCR_FORMAT_V8U8Y8A8:
        if (vs->chroma_type != VDP_CHROMA_TYPE_444)
            goto chroma;

        VDPAU_DBG("YUYV");
        if (vs->width != source_pitches[0]) {
            VDPAU_DBG("YUYV %d, %d", vs->width, source_pitches[0]);
        }

        if (source_ycbcr_format == VDP_YCBCR_FORMAT_Y8U8V8A8)
            shader = &vs->device->egl.yuv8444_rgb;
        else
            shader = &vs->device->egl.vuy8444_rgb;

        shader_init(vs, shader);

        /* yuv component */
        glActiveTexture(GL_TEXTURE0);
        CHECKEGL
        glBindTexture (GL_TEXTURE_2D, vs->y_tex);
        CHECKEGL
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vs->width,
                     vs->height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, source_data[0]);
        CHECKEGL
        glUniform1i (shader->texture[0], 0);
        CHECKEGL

        shader_draw(vs);
        break;

    case VDP_YCBCR_FORMAT_NV12:
        if (vs->chroma_type != VDP_CHROMA_TYPE_420)
            goto chroma;

        VDPAU_DBG("NV12");
        if (vs->width != source_pitches[0]) {
            VDPAU_DBG("NV12 %d, %d %d", vs->width, source_pitches[0], source_pitches[1]);
        }

        shader = &vs->device->egl.yuvnv12_rgb;
        shader_init(vs, shader);

        /* y component */
        glActiveTexture(GL_TEXTURE0);
        CHECKEGL
        glBindTexture (GL_TEXTURE_2D, vs->y_tex);
        CHECKEGL
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, vs->width,
                     vs->height, 0, GL_LUMINANCE,
                     GL_UNSIGNED_BYTE, source_data[0]);
        CHECKEGL
        glUniform1i (shader->texture[0], 0);
        CHECKEGL

        /* uv component */
        glActiveTexture(GL_TEXTURE1);
        CHECKEGL
        glBindTexture (GL_TEXTURE_2D, vs->u_tex);
        CHECKEGL
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, vs->width/2,
                     vs->height/2, 0, GL_LUMINANCE_ALPHA,
                     GL_UNSIGNED_BYTE, source_data[1]);
        CHECKEGL
        glUniform1i (shader->texture[1], 1);
        CHECKEGL

        shader_draw(vs);
        break;

    case VDP_YCBCR_FORMAT_YV12:
        if (vs->chroma_type != VDP_CHROMA_TYPE_420)
            goto chroma;

        if (vs->width != source_pitches[0]) {
            VDPAU_DBG("YV12 %d, %d %d %d", vs->width, source_pitches[0], source_pitches[1], source_pitches[2]);
        }

        shader = &vs->device->egl.yuvi420_rgb;
        shader_init(vs, shader);

        /* y component */
        glActiveTexture(GL_TEXTURE0);
        CHECKEGL
        glBindTexture (GL_TEXTURE_2D, vs->y_tex);
        CHECKEGL
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, vs->width,
                     vs->height, 0, GL_LUMINANCE,
                     GL_UNSIGNED_BYTE, source_data[0]);
        CHECKEGL
        glUniform1i (shader->texture[0], 0);
        CHECKEGL

        /* u component */
        glActiveTexture(GL_TEXTURE1);
        CHECKEGL
        glBindTexture (GL_TEXTURE_2D, vs->u_tex);
        CHECKEGL
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, vs->width/2,
                     vs->height/2, 0, GL_LUMINANCE,
                     GL_UNSIGNED_BYTE, source_data[source_ycbcr_format == INTERNAL_YCBCR_FORMAT ? 1 : 2]);
        CHECKEGL
        glUniform1i (shader->texture[1], 1);
        CHECKEGL

        /* v component */
        glActiveTexture(GL_TEXTURE2);
        CHECKEGL
        glBindTexture (GL_TEXTURE_2D, vs->v_tex);
        CHECKEGL
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, vs->width/2,
                     vs->height/2, 0, GL_LUMINANCE,
                     GL_UNSIGNED_BYTE, source_data[source_ycbcr_format == INTERNAL_YCBCR_FORMAT ? 2 : 1]);
        CHECKEGL
        glUniform1i (shader->texture[2], 2);
        CHECKEGL

        shader_draw(vs);
        break;
    }

    if (!eglMakeCurrent(vs->device->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
        VDPAU_ERR("Could not set EGL context to none %x", eglGetError());
        return VDP_STATUS_ERROR;
    }

    return VDP_STATUS_OK;

chroma:
    if (!eglMakeCurrent(vs->device->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
        VDPAU_ERR("Could not set EGL context to none %x", eglGetError());
    }

    return VDP_STATUS_INVALID_CHROMA_TYPE;
}

VdpStatus video_surface_render_picture(video_surface_ctx_t *vs,
                                             void **source_data)
{
    shader_ctx_t *shader = &vs->device->egl.yuvi420_rgb;
    shader_init(vs, shader);

    /* y component */
    glActiveTexture(GL_TEXTURE0);
    CHECKEGL
    glBindTexture (GL_TEXTURE_2D, vs->y_tex);
    CHECKEGL
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, vs->width,
                 vs->height, 0, GL_LUMINANCE,
                 GL_UNSIGNED_BYTE, source_data[0]);
    CHECKEGL
    glUniform1i (shader->texture[0], 0);
    CHECKEGL

    /* u component */
    glActiveTexture(GL_TEXTURE1);
    CHECKEGL
    glBindTexture (GL_TEXTURE_2D, vs->u_tex);
    CHECKEGL
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, vs->width/2,
                 vs->height/2, 0, GL_LUMINANCE,
                 GL_UNSIGNED_BYTE, source_data[1]);
    CHECKEGL
    glUniform1i (shader->texture[1], 1);
    CHECKEGL

    /* v component */
    glActiveTexture(GL_TEXTURE2);
    CHECKEGL
    glBindTexture (GL_TEXTURE_2D, vs->v_tex);
    CHECKEGL
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, vs->width/2,
                 vs->height/2, 0, GL_LUMINANCE,
                 GL_UNSIGNED_BYTE, source_data[2]);
    CHECKEGL
    glUniform1i (shader->texture[2], 2);
    CHECKEGL

    shader_draw(vs);
    return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_query_capabilities(VdpDevice device,
                                               VdpChromaType surface_chroma_type,
                                               VdpBool *is_supported,
                                               uint32_t *max_width,
                                               uint32_t *max_height)
{
    if (!is_supported || !max_width || !max_height)
        return VDP_STATUS_INVALID_POINTER;

    device_ctx_t *dev = handle_get(device);
    if (!dev)
        return VDP_STATUS_INVALID_HANDLE;

    *is_supported = surface_chroma_type == VDP_CHROMA_TYPE_420 || surface_chroma_type == VDP_CHROMA_TYPE_422;
    *max_width = 8192;
    *max_height = 8192;

    return VDP_STATUS_OK;
}

VdpStatus vdp_video_surface_query_get_put_bits_y_cb_cr_capabilities(VdpDevice device,
                                                                    VdpChromaType surface_chroma_type,
                                                                    VdpYCbCrFormat bits_ycbcr_format,
                                                                    VdpBool *is_supported)
{
    if (!is_supported)
        return VDP_STATUS_INVALID_POINTER;

    device_ctx_t *dev = handle_get(device);
    if (!dev)
        return VDP_STATUS_INVALID_HANDLE;

    *is_supported = VDP_FALSE;

    return VDP_STATUS_OK;
}
