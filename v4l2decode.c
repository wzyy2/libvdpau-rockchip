#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <linux/videodev2.h>

#include "vdpau_private.h"

#include "v4l2.h"
#include "v4l2decode.h"

static void openDevices(v4l2_decoder_t *ctx);
static void cleanup(v4l2_decoder_t *ctx);

static __u32 get_codec(VdpDecoderProfile profile)
{
    switch (profile)
    {
    case VDP_DECODER_PROFILE_MPEG1:
        return V4L2_PIX_FMT_MPEG1;

    case VDP_DECODER_PROFILE_MPEG2_SIMPLE:
    case VDP_DECODER_PROFILE_MPEG2_MAIN:
        return V4L2_PIX_FMT_MPEG2;

    case VDP_DECODER_PROFILE_H264_BASELINE:
    case VDP_DECODER_PROFILE_H264_MAIN:
    case VDP_DECODER_PROFILE_H264_HIGH:
        return V4L2_PIX_FMT_H264;

    case VDP_DECODER_PROFILE_MPEG4_PART2_SP:
    case VDP_DECODER_PROFILE_MPEG4_PART2_ASP:
        return V4L2_PIX_FMT_MPEG4;
    }
    //            return V4L2_PIX_FMT_H263;
    //            return V4L2_PIX_FMT_XVID;
    return V4L2_PIX_FMT_H264;
}

static parser_mode_t get_mode(VdpDecoderProfile profile)
{
    switch (profile)
    {
    case VDP_DECODER_PROFILE_MPEG1:
    case VDP_DECODER_PROFILE_MPEG2_SIMPLE:
    case VDP_DECODER_PROFILE_MPEG2_MAIN:
        return MODE_MPEG12;

    case VDP_DECODER_PROFILE_H264_BASELINE:
    case VDP_DECODER_PROFILE_H264_MAIN:
    case VDP_DECODER_PROFILE_H264_HIGH:
        return MODE_H264;

    case VDP_DECODER_PROFILE_MPEG4_PART2_SP:
    case VDP_DECODER_PROFILE_MPEG4_PART2_ASP:
        return MODE_MPEG4;
    }
    //            return V4L2_PIX_FMT_H263;
    //            return V4L2_PIX_FMT_XVID;
    return MODE_H264;
}


void *decoder_open(VdpDecoderProfile profile, uint32_t width, uint32_t height)
{
    v4l2_decoder_t *ctx = calloc(1, sizeof(v4l2_decoder_t));
    struct v4l2_format fmt;

    ctx->width = width;
    ctx->height = height;

    ctx->decoderHandle = -1;
    ctx->converterHandle = -1;

    ctx->outputBuffersCount = -1;
    ctx->captureBuffersCount = -1;
    ctx->converterBuffersCount = -1;

    ctx->mode = get_mode(profile);
    ctx->codec = get_codec(profile);
    openDevices(ctx);

    ctx->parser = parse_stream_init(ctx->mode, STREAM_BUFFER_SIZE);

    // Setup mfc output
    // Set mfc output format
    memzero(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.pixelformat = ctx->codec;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage = STREAM_BUFFER_SIZE;
    fmt.fmt.pix_mp.num_planes = 1;
    if (ioctl(ctx->decoderHandle, VIDIOC_S_FMT, &fmt)) {
        VDPAU_ERR("Failed to setup for MFC decoding");
        cleanup(ctx);
        return NULL;
    }

    // Get mfc output format
    memzero(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (ioctl(ctx->decoderHandle, VIDIOC_G_FMT, &fmt)) {
        VDPAU_ERR("Get Format failed");
        cleanup(ctx);
        return NULL;
    }
    VDPAU_DBG("Setup MFC decoding buffer size=%u (requested=%u)", fmt.fmt.pix_mp.plane_fmt[0].sizeimage, STREAM_BUFFER_SIZE);

    // Request mfc output buffers
    ctx->outputBuffersCount = RequestBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, STREAM_BUFFER_CNT);
    if (ctx->outputBuffersCount == V4L2_ERROR) {
        VDPAU_ERR("REQBUFS failed on queue of MFC");
        cleanup(ctx);
        return NULL;
    }
    VDPAU_DBG("REQBUFS Number of MFC buffers is %d (requested %d)", ctx->outputBuffersCount, STREAM_BUFFER_CNT);

    // Memory Map mfc output buffers
    ctx->outputBuffers = (v4l2_buffer_t *)calloc(ctx->outputBuffersCount, sizeof(v4l2_buffer_t));
    if(!ctx->outputBuffers) {
        VDPAU_ERR("cannot allocate buffers\n");
        cleanup(ctx);
        return NULL;
    }
    if(!MmapBuffers(ctx->decoderHandle, ctx->outputBuffersCount, ctx->outputBuffers, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, FALSE)) {
        VDPAU_ERR("cannot mmap output buffers\n");
        cleanup(ctx);
        return NULL;
    }
    VDPAU_DBG("Succesfully mmapped %d buffers", ctx->outputBuffersCount);

    return ctx;
}

void decoder_close(void *private)
{
    v4l2_decoder_t *ctx = (v4l2_decoder_t*)private;
    cleanup(ctx);

    free(ctx->parser);
    free(ctx);
}

static int process_header(v4l2_decoder_t *ctx, uint32_t buffer_count,
                    VdpBitstreamBuffer const *buffers);

static VdpStatus process_frames(v4l2_decoder_t *ctx, uint32_t buffer_count,
                    VdpBitstreamBuffer const *buffers, VdpVideoSurface output);

VdpStatus decoder_decode(void *private, uint32_t buffer_count,
                    VdpBitstreamBuffer const *buffers, VdpVideoSurface output)
{
    v4l2_decoder_t *ctx = (v4l2_decoder_t*)private;

    if (!ctx->headerProcessed) {
        int ret = process_header(ctx, buffer_count, buffers);
        if(ret)
            return ret < 0 ? VDP_STATUS_ERROR : VDP_STATUS_OK;
        if (ctx->codec == V4L2_PIX_FMT_H263)
            return process_frames(ctx, buffer_count, buffers, output);
        return VDP_STATUS_OK;
    }

    return process_frames(ctx, buffer_count, buffers, output);
}




static void listFormats(v4l2_decoder_t *ctx)
{
    // we enumerate all the supported formats looking for NV12MT and NV12
    int index = 0;
    while (1) {
        struct v4l2_fmtdesc vid_fmtdesc = {};
        vid_fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        vid_fmtdesc.index = index++;

        if (ioctl(ctx->decoderHandle, VIDIOC_ENUM_FMT, &vid_fmtdesc))
            break;

        VDPAU_DBG("Decoder format %d: %c%c%c%c (%s)", vid_fmtdesc.index,
            vid_fmtdesc.pixelformat & 0xFF, (vid_fmtdesc.pixelformat >> 8) & 0xFF,
            (vid_fmtdesc.pixelformat >> 16) & 0xFF, (vid_fmtdesc.pixelformat >> 24) & 0xFF,
            vid_fmtdesc.description);
        if (vid_fmtdesc.pixelformat == V4L2_PIX_FMT_NV12MT)
            ;
        if (vid_fmtdesc.pixelformat == V4L2_PIX_FMT_NV12)
            ;
    }
}

static void openDevices(v4l2_decoder_t *ctx)
{
    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir ("/sys/class/video4linux/")) != NULL) {
        while ((ent = readdir (dir)) != NULL) {
            if (strncmp(ent->d_name, "video", 5) == 0) {
                char *p;
                char name[64];
                char devname[64];
                char sysname[64];
                char drivername[32];
                char target[1024];
                int ret;

                snprintf(sysname, 64, "/sys/class/video4linux/%s", ent->d_name);
                snprintf(name, 64, "/sys/class/video4linux/%s/name", ent->d_name);

                FILE* fp = fopen(name, "r");
                if (fgets(drivername, 32, fp) != NULL) {
                    p = strchr(drivername, '\n');
                    if (p != NULL)
                        *p = '\0';
                } else {
                    fclose(fp);
                    continue;
                }
                fclose(fp);

                ret = readlink(sysname, target, sizeof(target));
                if (ret < 0)
                    continue;
                target[ret] = '\0';
                p = strrchr(target, '/');
                if (p == NULL)
                    continue;

                sprintf(devname, "/dev/%s", ++p);

                if (ctx->decoderHandle < 0 && strstr(drivername, "s5p-mfc-dec") != NULL) {
                    struct v4l2_capability cap;
                    int fd = open(devname, O_RDWR | O_NONBLOCK, 0);
                    if (fd > 0) {
                        memzero(cap);
                        if (!ioctl(fd, VIDIOC_QUERYCAP, &cap))
                            if ((cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE ||
                                    ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) && (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE))) &&
                                    (cap.capabilities & V4L2_CAP_STREAMING)) {
                                ctx->decoderHandle = fd;
                                VDPAU_DBG("Found %s %s", drivername, devname);
                            }
                  }
                  if (ctx->decoderHandle < 0)
                      close(fd);
                }
                if (ctx->converterHandle < 0 && strstr(drivername, "fimc") != NULL && strstr(drivername, "m2m") != NULL) {
                    struct v4l2_capability cap;
                    int fd = open(devname, O_RDWR | O_NONBLOCK, 0);
                    if (fd > 0) {
                        memzero(cap);
                        if (!ioctl(fd, VIDIOC_QUERYCAP, &cap))
                            if ((cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE ||
                                    ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) && (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE))) &&
                                    (cap.capabilities & V4L2_CAP_STREAMING)) {
                                ctx->converterHandle = fd;
                                VDPAU_DBG("Found %s %s", drivername, devname);
                            }
                    }
                    if (ctx->converterHandle < 0)
                        close(fd);
                }
                if (ctx->decoderHandle >= 0 && ctx->converterHandle >= 0) {
                    listFormats(ctx);
                    break;
                }
            }
        }
        closedir (dir);
    }
    return;
}

static void cleanup(v4l2_decoder_t *ctx)
{
    VDPAU_DBG("MUnmapping buffers");
    if (ctx->outputBuffers)
        ctx->outputBuffers = FreeBuffers(ctx->outputBuffersCount, ctx->outputBuffers);
    if (ctx->captureBuffers)
        ctx->captureBuffers = FreeBuffers(ctx->captureBuffersCount, ctx->captureBuffers);
    if (ctx->converterBuffers)
        ctx->converterBuffers = FreeBuffers(ctx->converterBuffersCount, ctx->converterBuffers);

    VDPAU_DBG("Devices cleanup");
    if (StreamOn(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMOFF))
        VDPAU_ERR("Stream OFF");
    if (StreamOn(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, VIDIOC_STREAMOFF))
        VDPAU_ERR("Stream OFF");
    if (StreamOn(ctx->converterHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMOFF))
        VDPAU_ERR("Stream OFF");
    if (StreamOn(ctx->converterHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, VIDIOC_STREAMOFF))
        VDPAU_ERR("Stream OFF");

    VDPAU_DBG("Closing devices");
    if (ctx->decoderHandle >= 0)
        close(ctx->decoderHandle);
    if (ctx->converterHandle >= 0)
        close(ctx->converterHandle);
}

static int process_header(v4l2_decoder_t *ctx, uint32_t buffer_count,
                    VdpBitstreamBuffer const *buffers)
{
    int size, ret, i;
    struct v4l2_format fmt;
    struct v4l2_control ctrl;
    struct v4l2_crop crop;

    int capturePlane1Size;
    int capturePlane2Size;
    int capturePlane3Size;

    for(i=0 ; i<buffer_count ; i++)
        parse_push(ctx->parser, buffers[i].bitstream, buffers[i].bitstream_bytes);

    ret = parse_stream(ctx->parser, ctx->outputBuffers[0].cPlane[0], STREAM_BUFFER_SIZE, &size, TRUE);
    if(ret <= 0)
        return ret;

    // Prepare header frame
    ctx->outputBuffers[0].iBytesUsed[0] = size;

    // Queue header to mfc output
    ret = QueueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, ctx->outputBuffers[0].iNumPlanes, 0, &ctx->outputBuffers[0]);
    if (ret == V4L2_ERROR) {
        VDPAU_ERR("queue input buffer");
        return -1;
    }
    ctx->outputBuffers[ret].bQueue = TRUE;
    VDPAU_DBG("<- %d header of size %d", ret, size);

    // STREAMON on mfc OUTPUT
    if (!StreamOn(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMON)) {
        VDPAU_ERR("Failed to Stream ON");
        return -1;
    }
    VDPAU_DBG("Stream ON");

    // Only need FIMC if we cannot set this capture pixel format to NV12M
    // Setup mfc capture
    memzero(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
    if(ioctl(ctx->decoderHandle, VIDIOC_TRY_FMT, &fmt)) {
        ctx->needConvert = 1;
        VDPAU_DBG("Direct decoding to untiled picture is NOT supported, FIMC conversion needed");
    } else {
        ctx->needConvert = 0;
        VDPAU_DBG("Direct decoding to untiled picture is supported, no conversion needed");
    }

    // Get mfc capture picture format
    memzero(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(ctx->decoderHandle, VIDIOC_G_FMT, &fmt)) {
        VDPAU_ERR("Failed to get format from");
        return -1;
    }
    capturePlane1Size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
    capturePlane2Size = fmt.fmt.pix_mp.plane_fmt[1].sizeimage;
    capturePlane3Size = fmt.fmt.pix_mp.plane_fmt[2].sizeimage;
    VDPAU_DBG("G_FMT: fmt (%dx%d), %c%c%c%c plane[0]=%d plane[1]=%d plane[2]=%d", fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
                        fmt.fmt.pix_mp.pixelformat & 0xFF, (fmt.fmt.pix_mp.pixelformat >> 8) & 0xFF,
                        (fmt.fmt.pix_mp.pixelformat >> 16) & 0xFF, (fmt.fmt.pix_mp.pixelformat >> 24) & 0xFF,
                        capturePlane1Size, capturePlane2Size, capturePlane3Size);

    if(ctx->needConvert) {
        // Setup FIMC OUTPUT fmt with data from MFC CAPTURE
        fmt.type                                = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        fmt.fmt.pix_mp.field                    = V4L2_FIELD_ANY;
        fmt.fmt.pix_mp.pixelformat              = V4L2_PIX_FMT_NV12MT;
        fmt.fmt.pix_mp.num_planes               = V4L2_NUM_MAX_PLANES;
        ret = ioctl(ctx->converterHandle, VIDIOC_S_FMT, &fmt);
        if (ret != 0) {
            VDPAU_ERR("Failed to SFMT on OUTPUT of FIMC");
            return -1;
        }
        VDPAU_DBG("S_FMT %dx%d", fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height);
    }

    // Get mfc needed number of buffers
    ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
    if (ioctl(ctx->decoderHandle, VIDIOC_G_CTRL, &ctrl)) {
        VDPAU_ERR("Failed to get the number of buffers required");
        return -1;
    }
    ctx->captureBuffersCount = (int)(ctrl.value * 1.5);

    // Get mfc capture crop
    memzero(crop);
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(ctx->decoderHandle, VIDIOC_G_CROP, &crop)) {
        VDPAU_ERR("Failed to get crop information");
        return -1;
    }
    VDPAU_DBG("G_CROP %dx%d", crop.c.width, crop.c.height);
    ctx->captureWidth = crop.c.width;
    ctx->captureHeight = crop.c.height;

    if(ctx->needConvert) {
        //setup FIMC OUTPUT crop with data from MFC CAPTURE
        crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        if (ioctl(ctx->converterHandle, VIDIOC_S_CROP, &crop)) {
            VDPAU_ERR("Failed to set CROP on OUTPUT");
            return -1;
        }
        VDPAU_DBG("S_CROP %dx%d", crop.c.width, crop.c.height);
    }

    // Request mfc capture buffers
    ctx->captureBuffersCount = RequestBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, ctx->captureBuffersCount);
    if (ctx->captureBuffersCount == V4L2_ERROR) {
        VDPAU_ERR("REQBUFS failed");
        return -1;
    }
    VDPAU_DBG("REQBUFS Number of buffers is %d", ctx->captureBuffersCount);

    // Memory Map and queue mfc capture buffers
    ctx->captureBuffers = (v4l2_buffer_t *)calloc(ctx->captureBuffersCount, sizeof(v4l2_buffer_t));
    if(!ctx->captureBuffers) {
        VDPAU_ERR("cannot allocate buffers");
        return -1;
    }
    if(!MmapBuffers(ctx->decoderHandle, ctx->captureBuffersCount, ctx->captureBuffers, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, TRUE)) {
        VDPAU_DBG("cannot mmap capture buffers");
        return -1;
    }
    for (i = 0; i < ctx->captureBuffersCount; i++) {
        ctx->captureBuffers[i].iBytesUsed[0] = capturePlane1Size;
        ctx->captureBuffers[i].iBytesUsed[1] = capturePlane2Size;
        ctx->captureBuffers[i].iBytesUsed[2] = capturePlane3Size;
        ctx->captureBuffers[i].bQueue = TRUE;
    }
    VDPAU_DBG("Succesfully mmapped and queued %d buffers", ctx->captureBuffersCount);

    // STREAMON on mfc CAPTURE
    if (!StreamOn(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, VIDIOC_STREAMON)) {
        VDPAU_ERR("Failed to Stream ON");
        return -1;
    }
    VDPAU_DBG("Stream ON");

    if(ctx->needConvert) {
        // Request fimc capture buffers
        ret = RequestBuffer(ctx->converterHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_USERPTR, ctx->captureBuffersCount);
        if (ret == V4L2_ERROR) {
            VDPAU_ERR("REQBUFS failed");
            return -1;
        }
        VDPAU_DBG("REQBUFS Number of buffers is %d", ret);

        // Setup fimc capture
        memzero(fmt);
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUV420M;
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width = ctx->width;
        fmt.fmt.pix_mp.height = ctx->height;
        fmt.fmt.pix_mp.num_planes = V4L2_NUM_MAX_PLANES;
        fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
        if (ioctl(ctx->converterHandle, VIDIOC_S_FMT, &fmt)) {
            VDPAU_ERR("Failed SFMT");
            return -1;
        }
        VDPAU_DBG("S_FMT %dx%d", fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height);

        // Setup FIMC CAPTURE crop
        memzero(crop);
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        crop.c.left = 0;
        crop.c.top = 0;
        crop.c.width = ctx->width;
        crop.c.height = ctx->height;
        if (ioctl(ctx->converterHandle, VIDIOC_S_CROP, &crop)) {
            VDPAU_ERR("Failed to set CROP on OUTPUT");
            return -1;
        }
        VDPAU_DBG("S_CROP %dx%d", crop.c.width, crop.c.height);
        memzero(fmt);
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (ioctl(ctx->converterHandle, VIDIOC_G_FMT, &fmt)) {
            VDPAU_ERR("Failed to get format from");
            return -1;
        }
        capturePlane1Size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;
        capturePlane2Size = fmt.fmt.pix_mp.plane_fmt[1].sizeimage;
        capturePlane3Size = fmt.fmt.pix_mp.plane_fmt[2].sizeimage;

        // Request fimc capture buffers
        ctx->converterBuffersCount = RequestBuffer(ctx->converterHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, CONVERTER_VIDEO_BUFFERS_CNT);
        if (ctx->converterBuffersCount == V4L2_ERROR) {
            VDPAU_ERR("REQBUFS failed");
            return -1;
        }
        VDPAU_DBG("REQBUFS Number of buffers is %d", ctx->converterBuffersCount);
        VDPAU_DBG("buffer parameters: plane[0]=%d plane[1]=%d plane[2]=%d", capturePlane1Size, capturePlane2Size, capturePlane3Size);

        // Memory Map and queue mfc capture buffers
        ctx->converterBuffers = (v4l2_buffer_t *)calloc(ctx->converterBuffersCount, sizeof(v4l2_buffer_t));
        if(!ctx->converterBuffers) {
            VDPAU_ERR("cannot allocate buffers");
            return -1;
        }
        if(!MmapBuffers(ctx->converterHandle, ctx->converterBuffersCount, ctx->converterBuffers, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, TRUE)) {
            VDPAU_ERR("cannot mmap capture buffers\n");
            return -1;
        }
        for (i = 0; i < ctx->converterBuffersCount; i++) {
            ctx->converterBuffers[i].iBytesUsed[0] = capturePlane1Size;
            ctx->converterBuffers[i].iBytesUsed[1] = capturePlane2Size;
            ctx->converterBuffers[i].iBytesUsed[2] = capturePlane3Size;
            ctx->converterBuffers[i].bQueue = TRUE;
        }
        VDPAU_DBG("Succesfully mmapped and queued %d buffers", ctx->converterBuffersCount);

        if (StreamOn(ctx->converterHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMON))
            VDPAU_DBG("Stream ON");
        else
            VDPAU_ERR("Failed to Stream ON");
        if (StreamOn(ctx->converterHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, VIDIOC_STREAMON))
            VDPAU_DBG("Stream ON");
        else
            VDPAU_ERR("Failed to Stream ON");
    }

    // Dequeue header on input queue
    ret = DequeueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, V4L2_NUM_MAX_PLANES);
    if (ret < 0) {
        VDPAU_ERR("error dequeue output buffer, got number %d, errno %d", ret, errno);
        return -1;
    }
    ctx->outputBuffers[ret].bQueue = FALSE;
    VDPAU_DBG("-> %d header", ret);

    ctx->headerProcessed = TRUE;
    return 0;
}

static VdpStatus process_frames(v4l2_decoder_t *ctx, uint32_t buffer_count,
                    VdpBitstreamBuffer const *buffers, VdpVideoSurface output)
{
    // MAIN LOOP
    int index = 0;
    int ret, i;

    for(i=0 ; i<buffer_count ; i++)
        parse_push(ctx->parser, buffers[i].bitstream, buffers[i].bitstream_bytes);

    while (index < ctx->outputBuffersCount && ctx->outputBuffers[index].bQueue)
        index++;

    if (index >= ctx->outputBuffersCount) { //all input buffers are busy, dequeue needed
        ret = PollOutput(ctx->decoderHandle, 1000); // POLLIN - Poll Capture, POLLOUT - Poll Output
        if (ret == V4L2_ERROR) {
            VDPAU_ERR("PollInput Error");
            return VDP_STATUS_ERROR;
        } else if (ret == V4L2_READY) {
            index = DequeueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, V4L2_NUM_MAX_PLANES);
            if (index < 0) {
                VDPAU_ERR("error dequeue output buffer, got number %d, errno %d", index, errno);
                return VDP_STATUS_ERROR;
            }
            ctx->outputBuffers[index].bQueue = FALSE;
            VDPAU_DBG("-> %d", index);
        } else if (ret == V4L2_BUSY) {
            index = -1;
        } else {
            VDPAU_ERR("PollOutput unexpected error, what the? %d", ret);
            return VDP_STATUS_ERROR;
        }
    }

    if (index >= 0) {
        // Parse frame, copy it to buffer
        int frameSize = 0;
        ret = parse_stream(ctx->parser, ctx->outputBuffers[index].cPlane[0], STREAM_BUFFER_SIZE, &frameSize, FALSE);
        if (ret < 0)
            return VDP_STATUS_ERROR;
        if (ret != 0) {
            ctx->outputBuffers[index].iBytesUsed[0] = frameSize;
            VDPAU_DBG("Extracted frame of size %d", frameSize);

            // Queue buffer into input queue
            ret = QueueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP, ctx->outputBuffers[index].iNumPlanes, index, &ctx->outputBuffers[index]);
            if (ret == V4L2_ERROR) {
                VDPAU_ERR("Failed to queue buffer with index %d, errno %d", index, errno);
                return VDP_STATUS_ERROR;
            }
            ctx->outputBuffers[index].bQueue = TRUE;
            VDPAU_DBG("%d <-", index);
        }
    }

    if(ctx->needConvert) {
        index = DequeueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, V4L2_NUM_MAX_PLANES);
        if (index < 0) {
            if (index != -EAGAIN) {// Dequeue buffer not ready, need more data on input. EAGAIN = 11
                VDPAU_ERR("error dequeue output buffer, got number %d", index);
                return VDP_STATUS_ERROR;
            }
        } else {
            ctx->captureBuffers[index].bQueue = FALSE;
            VDPAU_DBG("-> %d", index);

            //Process frame after mfc
            ret = QueueBuffer(ctx->converterHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_USERPTR, ctx->captureBuffers[index].iNumPlanes, index, &ctx->captureBuffers[index]);
            if (ret == V4L2_ERROR) {
                VDPAU_ERR("Failed to queue buffer with index %d", index);
                return VDP_STATUS_ERROR;
            }
            ctx->captureBuffers[ret].bQueue = TRUE;
            VDPAU_DBG("%d <-", ret);
       }

        // Dequeue frame from fimc output and pass it back to mfc capture
        index = DequeueBuffer(ctx->converterHandle, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_USERPTR, V4L2_NUM_MAX_PLANES);
        if (index >= 0) {
            VDPAU_DBG("-> %d", index);
            ctx->captureBuffers[index].bQueue = FALSE;

            ret = QueueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, ctx->captureBuffers[index].iNumPlanes, index, &ctx->captureBuffers[index]);
            if (ret == V4L2_ERROR) {
                VDPAU_ERR("Failed to queue buffer with index %d, errno = %d", index, errno);
                return VDP_STATUS_ERROR;
            }
            ctx->captureBuffers[ret].bQueue = TRUE;
            VDPAU_DBG("<- %d", ret);
        }
    }
    return VDP_STATUS_OK;
}

VdpStatus get_picture(void *context, video_surface_ctx_t *output) {
    v4l2_decoder_t *ctx = (v4l2_decoder_t *)context;
    int index = 0;
    int ret;

    if(ctx->needConvert) {
        index = DequeueBuffer(ctx->converterHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, V4L2_NUM_MAX_PLANES);
        if (index < 0) {
            if (index == -EAGAIN) // Dequeue buffer not ready, need more data on input. EAGAIN = 11
                return VDP_STATUS_OK;
            if (errno == 22)
                return VDP_STATUS_OK;
            VDPAU_ERR("error dequeue output buffer, got number %d %d", index, errno);
            return VDP_STATUS_ERROR;
        }
        VDPAU_DBG("-> %d", index);
        ctx->converterBuffers[index].bQueue = FALSE;

        internal_vdp_video_surface_put_bits_y_cb_cr(output, INTERNAL_YCBCR_FORMAT,
                                           ctx->converterBuffers[index].cPlane,
                                           ctx->converterBuffers[index].iSize);

        ret = QueueBuffer(ctx->converterHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, ctx->converterBuffers[index].iNumPlanes, index, &ctx->converterBuffers[index]);
        if (ret == V4L2_ERROR) {
            VDPAU_ERR("Failed to queue buffer with index %d, errno = %d", index, errno);
            return VDP_STATUS_ERROR;
        }
        ctx->converterBuffers[index].bQueue = TRUE;
        VDPAU_DBG("%d <-", index);
    } else {
        index = DequeueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, V4L2_NUM_MAX_PLANES);
        if (index < 0) {
            if (index == -EAGAIN) {// Dequeue buffer not ready, need more data on input. EAGAIN = 11
                VDPAU_DBG("again...");
                return VDP_STATUS_OK;
            }
            VDPAU_ERR("error dequeue output buffer, got number %d", index);
            return VDP_STATUS_ERROR;
        }
        ctx->captureBuffers[index].bQueue = FALSE;
        VDPAU_DBG("-> %d", index);

        internal_vdp_video_surface_put_bits_y_cb_cr(output, VDP_YCBCR_FORMAT_NV12,
                                           ctx->captureBuffers[index].cPlane,
                                           ctx->captureBuffers[index].iSize);

        ret = QueueBuffer(ctx->decoderHandle, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP, ctx->captureBuffers[index].iNumPlanes, index, &ctx->captureBuffers[index]);
        if (ret == V4L2_ERROR) {
            VDPAU_ERR("Failed to queue buffer with index %d, errno = %d", index, errno);
            return VDP_STATUS_ERROR;
        }
        ctx->captureBuffers[ret].bQueue = TRUE;
        VDPAU_DBG("<- %d", ret);
    }

    return VDP_STATUS_OK;
}
