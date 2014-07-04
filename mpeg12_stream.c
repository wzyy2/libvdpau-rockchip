#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <vdpau/vdpau.h>

#include "bs.h"

mpeg12_header(int width, int height, VdpDecoderProfile profile, VdpPictureInfoH264 *vdppi, uint8_t* buf, int size) {
    unsigned int vbv_buffer_size, fps, v;
    int i, constraint_parameter_flag;
    uint64_t time_code;
    int64_t best_aspect_error = INT64_MAX;
    AVRational aspect_ratio = s->avctx->sample_aspect_ratio;

    if (aspect_ratio.num == 0 || aspect_ratio.den == 0)
        bs_write_u8(b, 0x00);
        bs_write_u8(b, 0x00);
        bs_write_u8(b, 0x01);
        bs_write_u8(b, SEQ_START_CODE);

        bs_write_u(b, 12, width  & 0xFFF);
        bs_write_u(b, 12, height & 0xFFF);

        bs_write_u(b, 4, 1);
        bs_write_u(b, 4, 1);

        v = 0x3FFFF;
        bs_write_u(b, 18, v);
        bs_write_u(b, 1, 1);         // marker
        bs_write_u(b, 10, 3);   //vbv_buffer_size);

        constraint_parameter_flag =
            width  <= 768                                    &&
            height <= 576                                    &&
            v <= 1856000 / 400                               &&
            profile == VDP_DECODER_PROFILE_MPEG1;

        put_bits(&s->pb, 1, constraint_parameter_flag);

        ff_write_quant_matrix(&s->pb, s->avctx->intra_matrix);
        ff_write_quant_matrix(&s->pb, s->avctx->inter_matrix);

        if (s->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            AVFrameSideData *side_data;
            int width = s->width;
            int height = s->height;
            int use_seq_disp_ext;

            put_header(s, EXT_START_CODE);
            put_bits(&s->pb, 4, 1);                 // seq ext

            put_bits(&s->pb, 1, s->avctx->profile == 0); // escx 1 for 4:2:2 profile

            put_bits(&s->pb, 3, s->avctx->profile); // profile
            put_bits(&s->pb, 4, s->avctx->level);   // level

            put_bits(&s->pb, 1, s->progressive_sequence);
            put_bits(&s->pb, 2, s->chroma_format);
            put_bits(&s->pb, 2, s->width  >> 12);
            put_bits(&s->pb, 2, s->height >> 12);
            put_bits(&s->pb, 12, v >> 18);          // bitrate ext
            put_bits(&s->pb, 1, 1);                 // marker
            put_bits(&s->pb, 8, vbv_buffer_size >> 10); // vbv buffer ext
            put_bits(&s->pb, 1, s->low_delay);
            put_bits(&s->pb, 2, s->mpeg2_frame_rate_ext.num-1); // frame_rate_ext_n
            put_bits(&s->pb, 5, s->mpeg2_frame_rate_ext.den-1); // frame_rate_ext_d

            side_data = av_frame_get_side_data(s->current_picture_ptr->f, AV_FRAME_DATA_PANSCAN);
            if (side_data) {
                AVPanScan *pan_scan = (AVPanScan *)side_data->data;
                if (pan_scan->width && pan_scan->height) {
                    width = pan_scan->width >> 4;
                    height = pan_scan->height >> 4;
                }
            }

            use_seq_disp_ext = (width != s->width ||
                                height != s->height ||
                                s->avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
                                s->avctx->color_trc != AVCOL_TRC_UNSPECIFIED ||
                                s->avctx->colorspace != AVCOL_SPC_UNSPECIFIED);

            if (s->seq_disp_ext == 1 || (s->seq_disp_ext == -1 && use_seq_disp_ext)) {
                put_header(s, EXT_START_CODE);
                put_bits(&s->pb, 4, 2);                         // sequence display extension
                put_bits(&s->pb, 3, 0);                         // video_format: 0 is components
                put_bits(&s->pb, 1, 1);                         // colour_description
                put_bits(&s->pb, 8, s->avctx->color_primaries); // colour_primaries
                put_bits(&s->pb, 8, s->avctx->color_trc);       // transfer_characteristics
                put_bits(&s->pb, 8, s->avctx->colorspace);      // matrix_coefficients
                put_bits(&s->pb, 14, width);                    // display_horizontal_size
                put_bits(&s->pb, 1, 1);                         // marker_bit
                put_bits(&s->pb, 14, height);                   // display_vertical_size
                put_bits(&s->pb, 3, 0);                         // remaining 3 bits are zero padding
            }
        }
}
