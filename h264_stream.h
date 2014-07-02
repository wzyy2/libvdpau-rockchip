/*
 * h264bitstream - a library for reading and writing H.264 video
 * Copyright (C) 2005-2007 Auroras Entertainment, LLC
 * Copyright (C) 2008-2011 Avail-TVN
 *
 * Written by Alex Izvorski <aizvorski@gmail.com> and Alex Giladi <alex.giladi@gmail.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _H264_STREAM_H
#define _H264_STREAM_H        1

#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#include <vdpau/vdpau.h>

#include "bs.h"

int write_nal_unit(int nal_unit_type, int width, int height, VdpDecoderProfile profile, VdpPictureInfoH264 *vdppi, uint8_t* buf, int size);

void write_seq_parameter_set_rbsp(int width, int height, VdpDecoderProfile profile, VdpPictureInfoH264* sps, bs_t* b);
void write_scaling_list(bs_t* b, uint8_t* scalingList, int sizeOfScalingList, int useDefaultScalingMatrixFlag );

void write_pic_parameter_set_rbsp(VdpPictureInfoH264* pps, bs_t* b);

void write_rbsp_trailing_bits(bs_t* b);

//NAL ref idc codes
#define NAL_REF_IDC_PRIORITY_HIGHEST    3
#define NAL_REF_IDC_PRIORITY_HIGH       2
#define NAL_REF_IDC_PRIORITY_LOW        1
#define NAL_REF_IDC_PRIORITY_DISPOSABLE 0

//Table 7-1 NAL unit type codes
#define NAL_UNIT_TYPE_SPS                            7    // Sequence parameter set
#define NAL_UNIT_TYPE_PPS                            8    // Picture parameter set

#define H264_PROFILE_BASELINE  66
#define H264_PROFILE_MAIN      77
#define H264_PROFILE_EXTENDED  88
#define H264_PROFILE_HIGH     100

#endif
