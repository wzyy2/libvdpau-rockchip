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

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "vdpau_private.h"
#include "h264_stream.h"

static VdpStatus decode_h264(struct decoder_ctx_struct *dec, VdpPictureInfo const *info, uint32_t buffer_count,
                      VdpBitstreamBuffer const *buffers, VdpVideoSurface output);

static VdpStatus decode_raw(struct decoder_ctx_struct *dec, VdpPictureInfo const *info, uint32_t buffer_count,
                      VdpBitstreamBuffer const *buffers, VdpVideoSurface output);

VdpStatus vdp_decoder_create(VdpDevice device,
                             VdpDecoderProfile profile,
                             uint32_t width,
                             uint32_t height,
                             uint32_t max_references,
                             VdpDecoder *decoder)
{
    device_ctx_t *dev = handle_get(device);
    if (!dev)
        return VDP_STATUS_INVALID_HANDLE;

    if (max_references > 16)
        return VDP_STATUS_ERROR;

    decoder_ctx_t *dec = calloc(1, sizeof(decoder_ctx_t));
    if (!dec)
        goto err_ctx;

    dec->device = dev;
    dec->profile = profile;
    dec->width = width;
    dec->height = height;

    VdpStatus ret = VDP_STATUS_OK;
    switch (profile)
    {
    case VDP_DECODER_PROFILE_MPEG1:
    case VDP_DECODER_PROFILE_MPEG2_SIMPLE:
    case VDP_DECODER_PROFILE_MPEG2_MAIN:
        dec->decode = decode_raw;
        break;

    case VDP_DECODER_PROFILE_H264_BASELINE:
    case VDP_DECODER_PROFILE_H264_MAIN:
    case VDP_DECODER_PROFILE_H264_HIGH:
        dec->decode = decode_h264;
        break;

    case VDP_DECODER_PROFILE_MPEG4_PART2_SP:
    case VDP_DECODER_PROFILE_MPEG4_PART2_ASP:
        dec->decode = decode_raw;
        break;

    default:
        ret = VDP_STATUS_INVALID_DECODER_PROFILE;
        break;
    }

    char *debug = getenv("VDPAU_DEBUG");
    if (debug) {
        if (strstr(debug, "dump"))
            dec->debug |= DEBUG_DECODE_DUMP;
        if (strstr(debug, "raw")) {
            dec->debug |= DEBUG_DECODE_RAW;
            if(truncate("vid.raw", 0)) {
                if (errno != ENOENT) {
                    goto err_data;
                }
            }
        }
    }

    if (ret != VDP_STATUS_OK)
        goto err_data;

    int handle = handle_create(dec);
    if (handle == -1)
        goto err_data;

    dec->private = decoder_open(profile, width, height);

    *decoder = handle;
    return VDP_STATUS_OK;

err_data:
    free(dec);
err_ctx:
    return VDP_STATUS_RESOURCES;
}

VdpStatus vdp_decoder_destroy(VdpDecoder decoder)
{
    decoder_ctx_t *dec = handle_get(decoder);
    if (!dec)
        return VDP_STATUS_INVALID_HANDLE;

    decoder_close(dec->private);

    free(dec->header);
    free(dec->last_header);

    handle_destroy(decoder);
    free(dec);

    return VDP_STATUS_OK;
}

VdpStatus vdp_decoder_get_parameters(VdpDecoder decoder,
                                     VdpDecoderProfile *profile,
                                     uint32_t *width,
                                     uint32_t *height)
{
    decoder_ctx_t *dec = handle_get(decoder);
    if (!dec)
        return VDP_STATUS_INVALID_HANDLE;

    if (profile)
        *profile = dec->profile;

    if (width)
        *width = dec->width;

    if (height)
        *height = dec->height;

    return VDP_STATUS_OK;
}

VdpStatus vdp_decoder_render(VdpDecoder decoder,
                             VdpVideoSurface target,
                             VdpPictureInfo const *picture_info,
                             uint32_t bitstream_buffer_count,
                             VdpBitstreamBuffer const *bitstream_buffers)
{
    decoder_ctx_t *dec = handle_get(decoder);
    if (!dec)
        return VDP_STATUS_INVALID_HANDLE;

    video_surface_ctx_t *vid = handle_get(target);
    if (!vid)
        return VDP_STATUS_INVALID_HANDLE;

    vid->source_format = INTERNAL_YCBCR_FORMAT;

    if (dec->decode)
        return dec->decode(dec, picture_info, bitstream_buffer_count, bitstream_buffers, target);

    return VDP_STATUS_OK;
}

static VdpStatus decode_h264(struct decoder_ctx_struct *dec, VdpPictureInfo const *info, uint32_t buffer_count,
                      VdpBitstreamBuffer const *buffers, VdpVideoSurface output) {
    if (dec->header == NULL)
        dec->header = calloc(256, 1);
    dec->header_len = write_nal_unit(NAL_UNIT_TYPE_SPS, dec->width, dec->height, dec->profile, (VdpPictureInfoH264*)info, dec->header, 256);
    dec->header_len += write_nal_unit(NAL_UNIT_TYPE_PPS, dec->width, dec->height, dec->profile, (VdpPictureInfoH264*)info, dec->header + dec->header_len, 256 - dec->header_len);

    if (dec->last_header == NULL || dec->header_len != dec->last_header_len || memcmp(dec->last_header, dec->header, dec->header_len)) {
        VdpBitstreamBuffer buffer;
        buffer.struct_version = VDP_BITSTREAM_BUFFER_VERSION;
        buffer.bitstream = dec->header;
        buffer.bitstream_bytes = dec->header_len;
        decode_raw(dec, info, 1, &buffer, output);

        if (dec->last_header)
            free(dec->last_header);
        dec->last_header = dec->header;
        dec->header = NULL;
        dec->last_header_len = dec->header_len;
    }
    return decode_raw(dec, info, buffer_count, buffers, output);
}

static VdpStatus decode_raw(struct decoder_ctx_struct *dec, VdpPictureInfo const *info, uint32_t buffer_count,
                      VdpBitstreamBuffer const *buffers, VdpVideoSurface output) {
    unsigned int i;

    if (dec->debug & DEBUG_DECODE_DUMP) {
        for (i = 0; i < buffer_count; i++) {
            static char buf[256];
            int j;
            for (j=0 ; j<(buffers[i].bitstream_bytes < 16 ? buffers[i].bitstream_bytes : 16) ; j++) {
                snprintf(buf + j*3, 256, "%02x ", ((uint8_t*)buffers[i].bitstream)[j]);
            }
            VDPAU_DBG("%3d %8d: %s", i, buffers[i].bitstream_bytes, buf);
        }
    }

    if (dec->debug & DEBUG_DECODE_RAW) {
        FILE *f = fopen("vid.raw", "a+");
        for (i = 0; i < buffer_count; i++)
        {
            fwrite(buffers[i].bitstream, 1, buffers[i].bitstream_bytes, f);
        }
        fclose(f);
    }

    return decoder_decode(dec->private, buffer_count, buffers, output);
}

VdpStatus vdp_decoder_query_capabilities(VdpDevice device,
                                         VdpDecoderProfile profile,
                                         VdpBool *is_supported,
                                         uint32_t *max_level,
                                         uint32_t *max_macroblocks,
                                         uint32_t *max_width,
                                         uint32_t *max_height)
{
    if (!is_supported || !max_level || !max_macroblocks || !max_width || !max_height)
        return VDP_STATUS_INVALID_POINTER;

    device_ctx_t *dev = handle_get(device);
    if (!dev)
        return VDP_STATUS_INVALID_HANDLE;

    // guessed in lack of documentation, bigger pictures should be possible
    *max_level = 16;
    *max_width = 3840;
    *max_height = 2160;
    *max_macroblocks = (*max_width * *max_height) / (16 * 16);

    switch (profile)
    {
    case VDP_DECODER_PROFILE_MPEG1:
    case VDP_DECODER_PROFILE_MPEG2_SIMPLE:
    case VDP_DECODER_PROFILE_MPEG2_MAIN:
    case VDP_DECODER_PROFILE_H264_BASELINE:
    case VDP_DECODER_PROFILE_H264_MAIN:
    case VDP_DECODER_PROFILE_H264_HIGH:
    case VDP_DECODER_PROFILE_MPEG4_PART2_SP:
    case VDP_DECODER_PROFILE_MPEG4_PART2_ASP:
        *is_supported = VDP_TRUE;
        break;

    default:
        *is_supported = VDP_FALSE;
        break;
    }

    return VDP_STATUS_OK;
}
