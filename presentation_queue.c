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
#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "vdpau_private.h"
#include "rgba.h"

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <libdrm/drm_fourcc.h>


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
    qt->drawable = dev->drawable = drawable;

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

VdpStatus render_overlay(device_ctx_t *dev, int fb_id, int fullscreen,
                            int src_w, int src_h, int clip_w, int clip_h)
{
    drmModeResPtr r;
    drmModePlaneResPtr pr;

    int crtc_x = 0, crtc_y = 0, crtc_w = 0, crtc_h = 0;
    int old_fb = 0;
    int ret = -1;
    int i, j;
    int plane_id = 0;
    int crtc = 0;

    Window win;

    /**
     * enable all planes
     */
    drmSetClientCap(dev->drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
#ifdef DRM_CLIENT_CAP_ATOMIC
    drmSetClientCap(dev->drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);
#endif

    /**
     * get drm res for crtc
     */
    r = drmModeGetResources(dev->drm_fd);
    if (!r || !r->count_crtcs)
        goto err_res;

    /**
     * find the last available crtc
     **/
    for (i = r->count_crtcs; i && !crtc; i --)
    {
        drmModeCrtcPtr c = drmModeGetCrtc(dev->drm_fd, r->crtcs[i - 1]);
        if (c && c->mode_valid)
        {
            crtc = i;
            crtc_x = c->x;
            crtc_y = c->y;
            crtc_w = c->width;
            crtc_h = c->height;
        }
        drmModeFreeCrtc(c);
    }

    /**
     * get plane res for plane
     */
    pr = drmModeGetPlaneResources(dev->drm_fd);
    if (!pr || !pr->count_planes)
        goto err_plane_res;

    /**
     * find available plane
     */
    for (i = 0; i < pr->count_planes; i++)
    {
        drmModePlanePtr p = drmModeGetPlane(dev->drm_fd, pr->planes[i]);
        if (p && p->possible_crtcs == crtc)
            for (j = 0; j < p->count_formats && !plane_id; j++)
                if (p->formats[j] == DRM_FORMAT_NV12)
                {
                    plane_id = pr->planes[i];
                    old_fb = p->fb_id;
                }
        drmModeFreePlane(p);
    }

    /**
     * failed to get crtc or plane
     */
    if (!crtc || ! plane_id)
        goto err_overlay;

    if (!fullscreen)
    {
        /**
         * get window's x y w h
         */
        XTranslateCoordinates(dev->display,
                dev->drawable,
                RootWindow(dev->display, dev->screen),
                0, 0, &crtc_x, &crtc_y, &win);

        XTranslateCoordinates(dev->display,
                dev->drawable,
                RootWindow(dev->display, dev->screen),
                clip_w, clip_h, &crtc_w, &crtc_h, &win);
    }

    ret = drmModeSetPlane(dev->drm_ctl_fd, plane_id,
            r->crtcs[crtc - 1], fb_id, 0,
            crtc_x, crtc_y, crtc_w, crtc_h,
            0, 0, (src_w ? src_w : crtc_w) << 16,
            (src_h ? src_h : crtc_h) << 16);

    if (dev->saved_fb < 0)
    {
        dev->saved_fb = old_fb;
        VDPAU_DBG ("store fb:%d", old_fb);
    } else {
        drmModeRmFB(dev->drm_ctl_fd, old_fb);
    }
err_overlay:
    drmModeFreePlaneResources(pr);
err_plane_res:
    drmModeFreeResources(r);
err_res:

    if (ret < 0)
        return VDP_STATUS_ERROR;

    return VDP_STATUS_OK;
}

VdpStatus close_overlay(device_ctx_t *dev)
{
    VDPAU_DBG ("restore fb:%d", dev->saved_fb);
    render_overlay(dev, dev->saved_fb, 1, 0, 0, 0, 0);
    dev->dsp_mode = NO_OVERLAY;

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

    if (os->vs && q->device->dsp_mode != NO_OVERLAY)
    {
        VdpStatus ret;

        ret = render_overlay(q->device, os->vs->fb_id,
                q->device->dsp_mode == OVERLAY_FULLSCREEN,
                os->vs->dec->coded_width, os->vs->dec->coded_height,
                clip_width, clip_height);
        if (ret != VDP_STATUS_OK)
        {
            VDPAU_ERR("Could not render overlay");
            q->device->dsp_mode = NO_OVERLAY;
        }
        else
            return VDP_STATUS_OK;
    }

    if (!eglMakeCurrent(q->device->egl.display, q->target->surface,
                        q->target->surface, q->target->context)) {
        VDPAU_DBG ("Could not set EGL context to current %x", eglGetError());
        return VDP_STATUS_RESOURCES;
    }
    CHECKEGL

        /*
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
    CHECKEGL
    */



    if (os->vs && q->device->dsp_mode == NO_OVERLAY)
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

        video_surface_ctx_t *vs = os->vs;

        shader_ctx_t * shader = &vs->device->egl.oes;

        EGLint attrs[] = {
            EGL_WIDTH,                     0, EGL_HEIGHT,                    0,
            EGL_LINUX_DRM_FOURCC_EXT,      0, EGL_DMA_BUF_PLANE0_FD_EXT,     0,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0, EGL_DMA_BUF_PLANE0_PITCH_EXT,  0,
            EGL_DMA_BUF_PLANE1_FD_EXT,     0, EGL_DMA_BUF_PLANE1_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE1_PITCH_EXT,  0, EGL_YUV_COLOR_SPACE_HINT_EXT, 0,
            EGL_SAMPLE_RANGE_HINT_EXT, 0, EGL_NONE, };
        attrs[1] = vs->width;
        attrs[3] = vs->height;
        attrs[5] = DRM_FORMAT_NV12;
        attrs[7]  = vs->y_tex;
        attrs[9]  = 0;
        attrs[11] = vs->width;

        attrs[13] = vs->y_tex;
        attrs[15] = vs->width * vs->height;
        attrs[17] = vs->width;
        attrs[19] = EGL_ITU_REC601_EXT;
        attrs[21] = EGL_YUV_NARROW_RANGE_EXT;

        EGLImageKHR egl_image = eglCreateImageKHR(
                    q->device->egl.display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
        if (egl_image == EGL_NO_IMAGE_KHR)
            printf("jeffy, failed egl image\n");

        glClear (GL_COLOR_BUFFER_BIT);
        CHECKEGL

        glUseProgram (shader->program);
        CHECKEGL

        glViewport(os->video_dst_rect.x0, os->video_dst_rect.y0,
                os->video_dst_rect.x1-os->video_dst_rect.x0,
                os->video_dst_rect.y1-os->video_dst_rect.y0);

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


  int tex_id = 0;
  glGenTextures(1, &tex_id);
    CHECKEGL
            glActiveTexture(GL_TEXTURE0);
            CHECKEGL
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, tex_id);

    CHECKEGL
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    CHECKEGL
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    CHECKEGL
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    CHECKEGL
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CHECKEGL

    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, egl_image);

    CHECKEGL
    glUniform1i (shader->texture[0], 0);
     CHECKEGL


    //glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CHECKEGL
    glUseProgram(0);
    CHECKEGL
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    CHECKEGL
    eglSwapBuffers (q->device->egl.display, q->target->surface);
    CHECKEGL

    //eglDestroyImageKHR(q->device->egl.display, egl_image);

#if 0
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
        glActiveTexture(GL_TEXTURE1);
        CHECKEGL
        glBindTexture (GL_TEXTURE_EXTERNAL_OES, os->vs->rgb_tex);
        CHECKEGL
      //  glUniform1i (shader->texture[0], 3);
       // CHECKEGL

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        //glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
        CHECKEGL
        glBindTexture (GL_TEXTURE_EXTERNAL_OES, 0);

        //glUseProgram(0);
        CHECKEGL
#endif
    }

#if 0
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
        if (os->rgba.flags & RGBA_FLAG_CHANGED) {
            glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, os->rgba.width, os->rgba.height, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, os->rgba.data);
            CHECKEGL
            os->rgba.flags &= ~RGBA_FLAG_CHANGED;
        }
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
#endif

    //eglSwapBuffers (q->device->egl.display, q->target->surface);
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
