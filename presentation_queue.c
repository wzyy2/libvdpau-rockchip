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

#include <time.h>

#include "vdpau_private.h"
#include "rgba.h"

static uint64_t get_time(void)
{
    struct timespec tp;

    if (clock_gettime(CLOCK_MONOTONIC, &tp) == -1)
        return 0;

    return (uint64_t)tp.tv_sec * 1000000000ULL + (uint64_t)tp.tv_nsec;
}

VdpStatus vdp_presentation_queue_target_create_x11(VdpDevice device,
                                                   Drawable drawable,
                                                   VdpPresentationQueueTarget *target)
{
    if (!target || !drawable)
        return VDP_STATUS_INVALID_POINTER;

    device_ctx_t *dev = handle_get(device);
    if (!dev)
        return VDP_STATUS_INVALID_HANDLE;

    queue_target_ctx_t *qt = calloc(1, sizeof(queue_target_ctx_t));
    if (!qt)
        return VDP_STATUS_RESOURCES;

    qt->device = dev;
    qt->drawable = drawable;

    qt->surface = eglCreateWindowSurface(dev->egl.display, dev->egl.config,
                                     (EGLNativeWindowType)qt->drawable, NULL);
    if (qt->surface == EGL_NO_SURFACE) {
        VDPAU_DBG ("Could not create EGL surface");
        return VDP_STATUS_RESOURCES;
    }

    const EGLint contextAttribs[] =
    {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    qt->context = eglCreateContext(dev->egl.display, dev->egl.config,
                                     dev->egl.context, contextAttribs);
    if (qt->context == EGL_NO_CONTEXT) {
        VDPAU_DBG ("Could not create EGL context");
        return VDP_STATUS_RESOURCES;
    }

    if (!eglMakeCurrent(dev->egl.display, qt->surface,
                        qt->surface, qt->context)) {
        VDPAU_DBG ("Could not set EGL context to current %x", eglGetError());
        return VDP_STATUS_RESOURCES;
    }

    qt->overlay = gl_create_texture(GL_LINEAR);

    if (!eglMakeCurrent(dev->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
        VDPAU_DBG ("Could not set EGL context to none %x", eglGetError());
        return VDP_STATUS_RESOURCES;
    }

    VDPAU_DBG ("pq egl init done");

    XSetWindowBackground(dev->display, qt->drawable, 0x000102);

    int handle = handle_create(qt);
    if (handle == -1)
        goto out_handle_create;

    *target = handle;
    return VDP_STATUS_OK;

out_handle_create:
    free(qt);
    return VDP_STATUS_RESOURCES;
}

VdpStatus vdp_presentation_queue_target_destroy(VdpPresentationQueueTarget presentation_queue_target)
{
    queue_target_ctx_t *qt = handle_get(presentation_queue_target);
    if (!qt)
        return VDP_STATUS_INVALID_HANDLE;

    if (qt->context != EGL_NO_CONTEXT) {
        eglDestroyContext (qt->device->egl.display, qt->context);
    }

    if (qt->surface != EGL_NO_SURFACE) {
        eglDestroySurface (qt->device->egl.display, qt->surface);
    }

    handle_destroy(presentation_queue_target);
    free(qt);

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_create(VdpDevice device,
                                        VdpPresentationQueueTarget presentation_queue_target,
                                        VdpPresentationQueue *presentation_queue)
{
    if (!presentation_queue)
        return VDP_STATUS_INVALID_POINTER;

    device_ctx_t *dev = handle_get(device);
    if (!dev)
        return VDP_STATUS_INVALID_HANDLE;

    queue_target_ctx_t *qt = handle_get(presentation_queue_target);
    if (!qt)
        return VDP_STATUS_INVALID_HANDLE;

    queue_ctx_t *q = calloc(1, sizeof(queue_ctx_t));
    if (!q)
        return VDP_STATUS_RESOURCES;

    q->target = qt;
    q->device = dev;

    int handle = handle_create(q);
    if (handle == -1)
    {
        free(q);
        return VDP_STATUS_RESOURCES;
    }

    *presentation_queue = handle;
    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_destroy(VdpPresentationQueue presentation_queue)
{
    queue_ctx_t *q = handle_get(presentation_queue);
    if (!q)
        return VDP_STATUS_INVALID_HANDLE;

    handle_destroy(presentation_queue);
    free(q);

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_set_background_color(VdpPresentationQueue presentation_queue,
                                                      VdpColor *const background_color)
{
    if (!background_color)
        return VDP_STATUS_INVALID_POINTER;

    queue_ctx_t *q = handle_get(presentation_queue);
    if (!q)
        return VDP_STATUS_INVALID_HANDLE;

    q->background.red = background_color->red;
    q->background.green = background_color->green;
    q->background.blue = background_color->blue;
    q->background.alpha = background_color->alpha;

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_get_background_color(VdpPresentationQueue presentation_queue,
                                                      VdpColor *const background_color)
{
    if (!background_color)
        return VDP_STATUS_INVALID_POINTER;

    queue_ctx_t *q = handle_get(presentation_queue);
    if (!q)
        return VDP_STATUS_INVALID_HANDLE;

    background_color->red = q->background.red;
    background_color->green = q->background.green;
    background_color->blue = q->background.blue;
    background_color->alpha = q->background.alpha;

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_get_time(VdpPresentationQueue presentation_queue,
                                          VdpTime *current_time)
{
    queue_ctx_t *q = handle_get(presentation_queue);
    if (!q)
        return VDP_STATUS_INVALID_HANDLE;

    *current_time = get_time();
    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_display(VdpPresentationQueue presentation_queue,
                                         VdpOutputSurface surface,
                                         uint32_t clip_width,
                                         uint32_t clip_height,
                                         VdpTime earliest_presentation_time)
{
    queue_ctx_t *q = handle_get(presentation_queue);
    if (!q)
        return VDP_STATUS_INVALID_HANDLE;

    output_surface_ctx_t *os = handle_get(surface);
    if (!os)
        return VDP_STATUS_INVALID_HANDLE;

    if (earliest_presentation_time != 0)
        VDPAU_DBG_ONCE("Presentation time not supported");

    if (!eglMakeCurrent(q->device->egl.display, q->target->surface,
                        q->target->surface, q->target->context)) {
        VDPAU_DBG ("Could not set EGL context to current %x", eglGetError());
        return VDP_STATUS_RESOURCES;
    }
    CHECKEGL

    glBindFramebuffer (GL_FRAMEBUFFER, 0);
    CHECKEGL

    if (os->vs)
    {
        /* Do the GLES display of the video */
        GLfloat vVertices[] =
        {
            -1.0f, -1.0f,
            0.0f, 0.0f,

            1.0f, -1.0f,
            1.0f, 0.0f,

            1.0f, 1.0f,
            1.0f, 1.0f,

            -1.0f, 1.0f,
            0.0f, 1.0f,
        };
        GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER) ;
        if(status != GL_FRAMEBUFFER_COMPLETE) {
            VDPAU_DBG("failed to make complete framebuffer object %x", status);
        }

        shader_ctx_t *shader = &q->device->egl.copy;

        glClear (GL_COLOR_BUFFER_BIT);
        CHECKEGL

        glViewport(os->video_dst_rect.x0, os->video_dst_rect.y0,
                os->video_dst_rect.x1-os->video_dst_rect.x0,
                os->video_dst_rect.y1-os->video_dst_rect.y0);
        CHECKEGL

        glUseProgram (shader->program);
        CHECKEGL

        glVertexAttribPointer (shader->position_loc, 2, GL_FLOAT,
            GL_FALSE, 4 * sizeof (GLfloat), vVertices);
        CHECKEGL
        glEnableVertexAttribArray (shader->position_loc);
        CHECKEGL

        glVertexAttribPointer (shader->texcoord_loc, 2, GL_FLOAT,
            GL_FALSE, 4 * sizeof (GLfloat), &vVertices[2]);
        CHECKEGL
        glEnableVertexAttribArray (shader->texcoord_loc);
        CHECKEGL

        glActiveTexture(GL_TEXTURE3);
        CHECKEGL
        glBindTexture (GL_TEXTURE_2D, os->vs->rgb_tex);
        CHECKEGL
        glUniform1i (shader->texture[0], 3);
        CHECKEGL

        glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
        CHECKEGL

        glUseProgram(0);
        CHECKEGL
    }

    if (os->rgba.flags & RGBA_FLAG_NEEDS_CLEAR)
        rgba_clear(&os->rgba);

    if (os->rgba.flags & RGBA_FLAG_DIRTY)
    {
        GLfloat vVertices[] =
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
        GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

        shader_ctx_t *shader;
        if(os->rgba.format == VDP_RGBA_FORMAT_B8G8R8A8) {
            shader = &q->device->egl.brswap;
        } else {
            shader = &q->device->egl.copy;
        }

        glUseProgram (shader->program);
        CHECKEGL

        glViewport(0, 0, os->rgba.width, os->rgba.height);
        CHECKEGL

        glVertexAttribPointer (shader->position_loc, 2, GL_FLOAT,
            GL_FALSE, 4 * sizeof (GLfloat), vVertices);
        CHECKEGL
        glEnableVertexAttribArray (shader->position_loc);
        CHECKEGL

        glVertexAttribPointer (shader->texcoord_loc, 2, GL_FLOAT,
            GL_FALSE, 4 * sizeof (GLfloat), &vVertices[2]);
        CHECKEGL
        glEnableVertexAttribArray (shader->texcoord_loc);
        CHECKEGL

        glActiveTexture(GL_TEXTURE0);
        CHECKEGL
        glBindTexture (GL_TEXTURE_2D, q->target->overlay);
        CHECKEGL
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, os->rgba.width, os->rgba.height, 0, GL_RGBA,
                  GL_UNSIGNED_BYTE, os->rgba.data);
        CHECKEGL
        glUniform1i (shader->texture[0], 0);
        CHECKEGL

        glEnable(GL_BLEND);
        CHECKEGL
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        CHECKEGL

        glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
        CHECKEGL

        glUseProgram(0);
        CHECKEGL

        glDisable(GL_BLEND);
        CHECKEGL
    }

    eglSwapBuffers (q->device->egl.display, q->target->surface);
    eglMakeCurrent(q->device->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_block_until_surface_idle(VdpPresentationQueue presentation_queue,
                                                          VdpOutputSurface surface,
                                                          VdpTime *first_presentation_time)
{
    queue_ctx_t *q = handle_get(presentation_queue);
    if (!q)
        return VDP_STATUS_INVALID_HANDLE;

    output_surface_ctx_t *out = handle_get(surface);
    if (!out)
        return VDP_STATUS_INVALID_HANDLE;

    *first_presentation_time = get_time();

    return VDP_STATUS_OK;
}

VdpStatus vdp_presentation_queue_query_surface_status(VdpPresentationQueue presentation_queue,
                                                      VdpOutputSurface surface,
                                                      VdpPresentationQueueStatus *status,
                                                      VdpTime *first_presentation_time)
{
    queue_ctx_t *q = handle_get(presentation_queue);
    if (!q)
        return VDP_STATUS_INVALID_HANDLE;

    output_surface_ctx_t *out = handle_get(surface);
    if (!out)
        return VDP_STATUS_INVALID_HANDLE;

    *status = VDP_PRESENTATION_QUEUE_STATUS_VISIBLE;
    *first_presentation_time = get_time();

    return VDP_STATUS_OK;
}
