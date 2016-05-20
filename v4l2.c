#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/media.h>

#include "v4l2.h"

#define PRINT(...) \
    printf(__VA_ARGS__)

#define IOCTL_OR_ERROR_RETURN_VALUE(type, arg, value, type_str)           \
    do {                                                                    \
        if (ioctl(dec->fd, type, arg) != 0) {                                 \
            PRINT("%s(%d):ioctl() failed: %s\n", __func__, __LINE__, type_str); \
            return value;                                                       \
        }                                                                     \
    } while (0)

#define IOCTL_OR_ERROR_RETURN(type, arg) \
    IOCTL_OR_ERROR_RETURN_VALUE(type, arg, -1, #type)

#define IOCTL_OR_LOG_ERROR(type, arg)                                 \
    do {                                                                \
        if (ioctl(dec->fd, type, arg) != 0)                               \
        PRINT("(): ioctl() failed: " #type);                            \
    } while (0)


int v4l2_init(const char *device_path) {
    int fd = open(device_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);

    if (fd <= 0) {
        PRINT("failed to open %s\n", device_path);
        return 0;
    }


    PRINT(" got %s\n", device_path);

    return fd;
}

int v4l2_init_by_name(const char *name) {
    DIR *dir;
    struct dirent *ent;
    int fd = 0;

#define SYS_PATH		"/sys/class/video4linux/"
#define DEV_PATH		"/dev/"

    if ((dir = opendir(SYS_PATH)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            FILE *fp;
            char path[64];
            char dev_name[64];

            snprintf(path, 64, SYS_PATH "%s/name",
                    ent->d_name);
            fp = fopen(path, "r");
            if (!fp)
                continue;
            fgets(dev_name, 32, fp);
            fclose(fp);

            if (!strstr(dev_name, name))
                continue;

            snprintf(path, sizeof(path), DEV_PATH "%s",
                    ent->d_name);

            fd = v4l2_init(path);
            break;
        }
        closedir (dir);
    }
    return fd;
}

int v4l2_deinit(decoder_ctx_t *dec) {
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = 0;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;
    IOCTL_OR_LOG_ERROR(VIDIOC_REQBUFS, &reqbufs);

    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = 0;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;
    IOCTL_OR_ERROR_RETURN(VIDIOC_REQBUFS, &reqbufs);

    munmap(dec->input_buffer, dec->buffer_size);

    close(dec->fd);
    dec->fd = 0;

    return 0;
}

int v4l2_reqbufs(decoder_ctx_t *dec) {
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = 1;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;
    IOCTL_OR_ERROR_RETURN(VIDIOC_REQBUFS, &reqbufs);

    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = kOutputBufferCnt;
    reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbufs.memory = V4L2_MEMORY_MMAP;
    IOCTL_OR_ERROR_RETURN(VIDIOC_REQBUFS, &reqbufs);

    return 0;
}

int v4l2_querybuf(decoder_ctx_t *dec) {
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    struct v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    memset(planes, 0, sizeof(planes));
    buffer.index = 0;
    buffer.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.m.planes = planes;
    buffer.length = 1;
    IOCTL_OR_ERROR_RETURN(VIDIOC_QUERYBUF, &buffer);

    dec->buffer_size = buffer.m.planes[0].length;
    dec->input_buffer = mmap(NULL, dec->buffer_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED, dec->fd,
            buffer.m.planes[0].m.mem_offset);
    if (dec->input_buffer == MAP_FAILED) {
        PRINT("create input buffer: mmap() failed");
        return -1;
    }

    return 0;
}

int v4l2_expbuf(decoder_ctx_t *dec) {
    int i;

    for (i = 0; i < kOutputBufferCnt; i++) {
        struct v4l2_exportbuffer expbuf;
        memset(&expbuf, 0, sizeof(expbuf));
        expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        expbuf.index = i;
        expbuf.plane = 0;
        expbuf.flags = O_CLOEXEC | O_RDWR;
        IOCTL_OR_ERROR_RETURN(VIDIOC_EXPBUF, &expbuf);

        dec->output[i] = expbuf.fd;
    }

    return 0;
}

int v4l2_s_fmt_input(decoder_ctx_t *dec) {
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264_SLICE;
    format.fmt.pix_mp.plane_fmt[0].sizeimage = 4 * 1024 * 1024;
    format.fmt.pix_mp.num_planes = 1;
    IOCTL_OR_ERROR_RETURN(VIDIOC_S_FMT, &format);

    return 0;
}

int v4l2_s_fmt_output(decoder_ctx_t *dec) {
    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    format.fmt.pix_mp.width = dec->width;
    format.fmt.pix_mp.height = dec->height;
    format.fmt.pix_mp.num_planes = 1;
    IOCTL_OR_ERROR_RETURN(VIDIOC_S_FMT, &format);

    dec->coded_width = format.fmt.pix_mp.width;
    dec->coded_height = format.fmt.pix_mp.height;

    return 0;
}

int v4l2_streamon(decoder_ctx_t *dec) {
    __u32 type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    IOCTL_OR_ERROR_RETURN(VIDIOC_STREAMON, &type);

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    IOCTL_OR_ERROR_RETURN(VIDIOC_STREAMON, &type);

    return 0;
}

int v4l2_streamoff(decoder_ctx_t *dec) {
    __u32 type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    IOCTL_OR_ERROR_RETURN(VIDIOC_STREAMOFF, &type);

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    IOCTL_OR_ERROR_RETURN(VIDIOC_STREAMOFF, &type);

    return 0;
}

int v4l2_s_ext_ctrls(decoder_ctx_t *dec,
                     struct v4l2_ext_controls* ext_ctrls) {
    IOCTL_OR_ERROR_RETURN(VIDIOC_S_EXT_CTRLS, ext_ctrls);

    return 0;
}

int v4l2_qbuf_input(decoder_ctx_t *dec) {
    struct v4l2_buffer qbuf;
    struct v4l2_plane qbuf_planes[VIDEO_MAX_PLANES];
    memset(&qbuf, 0, sizeof(qbuf));
    memset(qbuf_planes, 0, sizeof(qbuf_planes));
    qbuf.index = 0;
    qbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.m.planes = qbuf_planes;
    qbuf.m.planes[0].bytesused = dec->buffer_size;
    qbuf.length = 1;
    IOCTL_OR_ERROR_RETURN(VIDIOC_QBUF, &qbuf);

    return 0;
}

int v4l2_qbuf_output(decoder_ctx_t *dec, int index) {
    struct v4l2_buffer qbuf;
    struct v4l2_plane qbuf_planes[VIDEO_MAX_PLANES];
    memset(&qbuf, 0, sizeof(qbuf));
    memset(qbuf_planes, 0, sizeof(qbuf_planes));
    qbuf.index = index;
    qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    qbuf.memory = V4L2_MEMORY_MMAP;
    qbuf.m.planes = qbuf_planes;
    qbuf.length = 1;
    IOCTL_OR_ERROR_RETURN(VIDIOC_QBUF, &qbuf);

    return 0;
}

int v4l2_dqbuf_input(decoder_ctx_t *dec) {
    struct v4l2_buffer dqbuf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    memset(&dqbuf, 0, sizeof(dqbuf));
    memset(&planes, 0, sizeof(planes));
    dqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    dqbuf.memory = V4L2_MEMORY_MMAP;
    dqbuf.m.planes = planes;
    dqbuf.length = 1;
    while (ioctl(dec->fd, VIDIOC_DQBUF, &dqbuf) != 0) {
        if (errno == EAGAIN) {
            continue;
        }
        PRINT("ioctl() failed: VIDIOC_DQBUF");
        return -1;
    }

    return 0;
}

int v4l2_dqbuf_output(decoder_ctx_t *dec) {
    struct v4l2_buffer dqbuf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];

    memset(&dqbuf, 0, sizeof(dqbuf));
    memset(&planes, 0, sizeof(planes));
    dqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    dqbuf.memory = V4L2_MEMORY_MMAP;
    dqbuf.m.planes = planes;
    dqbuf.length = 1;

    while (ioctl(dec->fd, VIDIOC_DQBUF, &dqbuf) != 0) {
        if (errno == EAGAIN) {
            usleep(1000);
            continue;
        }
        PRINT("ioctl() failed: VIDIOC_DQBUF");
        return -1;
    }

    return dqbuf.index;
}
