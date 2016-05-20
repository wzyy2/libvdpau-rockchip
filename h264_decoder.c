#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <linux/types.h>
#include <linux/v4l2-controls.h>

#include "v4l2.h"
#include "h264_decoder.h"

#include "h264d.h"

#define DEV_NAME_RK3399		    "rockchip-vpu-vdec"
#define DEV_NAME_RK3288_NEW	    "rockchip-vpu-dec"
#define DEV_NAME_RK3288_LEGACY	"rk3288-vpu-dec"

struct timeval last_tv;
struct timeval tv;

void log_time(char *msg)
{
#define TIME_TO_MS(tv) (tv.tv_sec * 1000 + tv.tv_usec / 1000)
#define DURATION(tv1, tv2) (TIME_TO_MS(tv2) - TIME_TO_MS(tv1))

    if (getenv("LOG_TIME")) {
        gettimeofday(&tv, NULL);
        if (msg)
            printf("\n%s: %ld ms\n", msg, DURATION(last_tv, tv));
        last_tv = tv;
    }
}

int h264_get_picture(void *p) {
    decoder_ctx_t *dec = (decoder_ctx_t *)p;

    return h264d_get_picture(dec->private);
}

void h264_release_picture(void *p, int index) {
    decoder_ctx_t *dec = (decoder_ctx_t *)p;

    while ((index = h264d_get_unrefed_picture(dec->private)) >= 0) {
        v4l2_qbuf_output(dec, index);
    }
}

void h264_submit(decoder_ctx_t* dec, VdpPictureInfoH264 const *info,
                 void *nal, size_t nal_size, int submit) {
    int is_frame = 0, i;
    int index;
    size_t num_ctrls = 0;
    uint32_t ctrl_ids[5];
    void *payloads[5];
    uint32_t payload_sizes[5];

    struct v4l2_ctrl_h264_sps *sps;
    struct v4l2_ctrl_h264_pps *pps;
    struct v4l2_ext_controls ext_ctrls;

    log_time("start decode");

    is_frame = h264d_prepare_data_raw(dec->private, nal,
            nal_size, &num_ctrls, ctrl_ids,
            payloads, payload_sizes);

    sps = (struct v4l2_ctrl_h264_sps *)payloads[0];
    pps = (struct v4l2_ctrl_h264_pps *)payloads[1];

    if (!is_frame || !submit)
        return;

#define COPY(param, field) (param->field = info->field)
#define COPY2(param, field, field2) (param->field = info->field2)

    COPY(pps, weighted_bipred_idc);
    COPY(pps, pic_init_qp_minus26);
    COPY(pps, chroma_qp_index_offset);
    COPY(pps, second_chroma_qp_index_offset);
    COPY2(pps, num_ref_idx_l0_default_active_minus1,
            num_ref_idx_l0_active_minus1);
    COPY2(pps, num_ref_idx_l1_default_active_minus1,
            num_ref_idx_l1_active_minus1);

    COPY(sps, log2_max_frame_num_minus4);
    COPY(sps, log2_max_pic_order_cnt_lsb_minus4);
    COPY(sps, pic_order_cnt_type);

    memset(&ext_ctrls, 0, sizeof(ext_ctrls));
    ext_ctrls.count = num_ctrls;
    ext_ctrls.controls = calloc(num_ctrls,
            sizeof(struct v4l2_ext_control));

    for (i = 0; i < num_ctrls; ++i) {
        ext_ctrls.controls[i].id = ctrl_ids[i];
        ext_ctrls.controls[i].ptr = payloads[i];
        ext_ctrls.controls[i].size = payload_sizes[i];
    }
    v4l2_s_ext_ctrls(dec, &ext_ctrls);

    free(ext_ctrls.controls);

    v4l2_qbuf_input(dec);

    log_time(NULL);

    if ((index = v4l2_dqbuf_output(dec)) >= 0) {
        h264d_picture_ready(dec->private, index);

        //to workaround plugin bug
        h264_release_picture(dec, index);
    }

    log_time("end decode");

    v4l2_dqbuf_input(dec);
}

VdpStatus h264_decode(decoder_ctx_t *dec, const VdpPictureInfoH264 *info,
                      uint32_t buffer_count,
                      VdpBitstreamBuffer const *buffers) {

    int i = 0;
    void *nal = dec->input_buffer;
    size_t nal_size = 0;

    for(i = 0; i < buffer_count; i++) {
#define START_CODE "\0\0\1"
        if (i && !memcmp(START_CODE, buffers[i].bitstream, 3)) {
            h264_submit(dec, info, nal, nal_size, 0);
            nal += nal_size;
            nal_size = 0;
        }
        memcpy(nal + nal_size, buffers[i].bitstream,
                buffers[i].bitstream_bytes);
        nal_size += buffers[i].bitstream_bytes;
    }

    h264_submit(dec, info, nal, nal_size, 1);

    return VDP_STATUS_OK;
}

static VdpStatus h264_start(struct decoder_ctx_struct *dec,
        VdpPictureInfo const *info) {
    int i;

    if (v4l2_s_fmt_output(dec) < 0)
        return VDP_STATUS_ERROR;

    if (v4l2_reqbufs(dec) < 0)
        return VDP_STATUS_ERROR;

    if (v4l2_querybuf(dec) < 0)
        return VDP_STATUS_ERROR;

    if (v4l2_expbuf(dec) < 0)
        return VDP_STATUS_ERROR;

    if (v4l2_streamon(dec) < 0)
        return VDP_STATUS_ERROR;

    for (i = 0; i < kOutputBufferCnt; i++) {
        v4l2_qbuf_output(dec, i);
    }

    dec->running = 1;

    return VDP_STATUS_OK;
}

static VdpStatus h264_pre_decode(void *p, VdpPictureInfo const *info,
                                 uint32_t buffer_count,
                                 VdpBitstreamBuffer const *buffers,
                                 VdpVideoSurface output) {
    decoder_ctx_t *dec = (decoder_ctx_t *)p;

    if (!dec->running) {
        h264_start(dec, info);
    }

    h264d_update_info(dec->private, dec->profile,
            dec->width, dec->height,
            (VdpPictureInfoH264 *)info);

    return h264_decode(dec,
            (const VdpPictureInfoH264 *)info,
            buffer_count, buffers);
}

void h264_deinit(void *p) {
    decoder_ctx_t *dec = (decoder_ctx_t *)p;

    v4l2_streamoff(dec);
    v4l2_deinit(dec);
    h264d_deinit(dec->private);
}

void *h264_init(decoder_ctx_t *dec) {
    dec->fd = v4l2_init_by_name(DEV_NAME_RK3399);
    if (dec->fd <= 0) {
        dec->fd = v4l2_init_by_name(DEV_NAME_RK3288_NEW);
        if (dec->fd <= 0) {
            dec->fd = v4l2_init_by_name(DEV_NAME_RK3288_LEGACY);
            if (dec->fd <= 0)
                return NULL;
        }
    }

    dec->decode = h264_pre_decode;
    dec->get_picture = h264_get_picture;
    dec->release_picture = h264_release_picture;
    dec->deinit = h264_deinit;

    if (v4l2_s_fmt_input(dec) < 0)
        return NULL;

    if (v4l2_s_fmt_output(dec) < 0)
        return NULL;

    return h264d_init();
}
