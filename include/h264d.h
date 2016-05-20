#ifndef H264D_H
#define H264D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vdpau/vdpau.h>

/* init & return priv ctx */
void *h264d_init(void);

/* prepare data for set ctrl, return ture if buffer is a frame */
int h264d_prepare_data_raw(void *dec, void *buffer, size_t size,
                           size_t *num_ctrls, uint32_t *ctrl_ids,
                           void **payloads, uint32_t *payload_sizes);

bool h264d_prepare_data(void *dec, struct v4l2_buffer *buffer,
                        size_t *num_ctrls, uint32_t *ctrl_ids,
                        void **payloads, uint32_t *payload_sizes);

void h264d_update_info(void *dec, VdpDecoderProfile profile,
                       int width, int height, VdpPictureInfoH264 *info);

/* a new picture decoded */
void h264d_picture_ready(void *dec, int index);

/* get a picture for display */
int h264d_get_picture(void *dec);

/* get a unrefed picture */
int h264d_get_unrefed_picture(void *dec);

/* delect priv ctx */
void h264d_deinit(void *dec);

#endif
