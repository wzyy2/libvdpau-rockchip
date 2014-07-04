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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "h264_stream.h"

FILE* h264_dbgfile = NULL;

#define printf(...) fprintf((h264_dbgfile == NULL ? stdout : h264_dbgfile), __VA_ARGS__)

/**
 Calculate the log base 2 of the argument, rounded up.
 Zero or negative arguments return zero
 Idea from http://www.southwindsgames.com/blog/2009/01/19/fast-integer-log2-function-in-cc/
 */
int intlog2(int x)
{
    int log = 0;
    if (x < 0) { x = 0; }
    while ((x >> log) > 0)
    {
        log++;
    }
    if (log > 0 && x == 1<<(log-1)) { log--; }
    return log;
}

/**
   Convert RBSP data to NAL data (Annex B format).
   The size of nal_buf must be 4/3 * the size of the rbsp_buf (rounded up) to guarantee the output will fit.
   If that is not true, output may be truncated and an error will be returned.
   If that is true, there is no possible error during this conversion.
   @param[in] rbsp_buf   the rbsp data
   @param[in] rbsp_size  pointer to the size of the rbsp data
   @param[in,out] nal_buf   allocated memory in which to put the nal data
   @param[in,out] nal_size  as input, pointer to the maximum size of the nal data; as output, filled in with the actual size of the nal data
   @return  actual size of nal data, or -1 on error
 */
// 7.3.1 NAL unit syntax
// 7.4.1.1 Encapsulation of an SODB within an RBSP
int rbsp_to_nal(const uint8_t* rbsp_buf, const int* rbsp_size, uint8_t* nal_buf, int* nal_size)
{
    int i;
    int j     = 1;
    int count = 0;

    if (*nal_size > 0) { nal_buf[0] = 0x00; } // zero out first byte since we start writing from second byte

    for ( i = 0; i < *rbsp_size ; i++ )
    {
        if ( j >= *nal_size )
        {
            // error, not enough space
            return -1;
        }

        if ( ( count == 2 ) && !(rbsp_buf[i] & 0xFC) ) // HACK 0xFC
        {
            nal_buf[j] = 0x03;
            j++;
            count = 0;
        }
        nal_buf[j] = rbsp_buf[i];
        if ( rbsp_buf[i] == 0x00 )
        {
            count++;
        }
        else
        {
            count = 0;
        }
        j++;
    }

    if (rbsp_buf[(*rbsp_size) -1] == 0x00) {
        nal_buf[j] = 0x03;
        j++;
    }

    *nal_size = j;
    return j;
}

/***************************** writing ******************************/

/**
 Write a NAL unit to a byte buffer.
 The NAL which is written out has a type determined by h->nal and data which comes from other fields within h depending on its type.
 @param[in,out]  h          the stream object
 @param[out]     buf        the buffer
 @param[in]      size       the size of the buffer
 @return                    the length of data actually written
 */
//7.3.1 NAL unit syntax
int write_nal_unit(int nal_unit_type, int width, int height, VdpDecoderProfile profile, VdpPictureInfoH264 *vdppi, uint8_t* buf, int size)
{
    #define HEADER_SIZE 3
    int rbsp_size = size*3/4; // NOTE this may have to be slightly smaller (3/4 smaller, worst case) in order to be guaranteed to fit
    uint8_t* rbsp_buf = (uint8_t*)calloc(1, rbsp_size); // FIXME can use malloc?
    int nal_size = size - HEADER_SIZE;

    bs_t* b = bs_new(rbsp_buf, rbsp_size);

    switch ( nal_unit_type )
    {
        case NAL_UNIT_TYPE_SPS:
            write_seq_parameter_set_rbsp(width, height, profile, vdppi, b);
            break;

        case NAL_UNIT_TYPE_PPS:
            write_pic_parameter_set_rbsp(vdppi, b);
            break;

        default:
            // here comes the reserved/unspecified/ignored stuff
            return 0;
    }


    if (bs_overrun(b)) { bs_free(b); free(rbsp_buf); return -1; }

    // now get the actual size used
    rbsp_size = bs_pos(b);

    int rc = rbsp_to_nal(rbsp_buf, &rbsp_size, buf + HEADER_SIZE, &nal_size);
    if (rc < 0) { bs_free(b); free(rbsp_buf); return -1; }

    bs_free(b);
    free(rbsp_buf);

    b = bs_new(buf, size);

    bs_write_u8(b, 0);
    bs_write_u8(b, 0);
    bs_write_u8(b, 1);

    bs_write_f(b,1, 0);
    bs_write_u(b,2, NAL_REF_IDC_PRIORITY_HIGHEST);
    bs_write_u(b,5, nal_unit_type);

    bs_free(b);

    return nal_size + HEADER_SIZE;
}


//7.3.2.1 Sequence parameter set RBSP syntax
void write_seq_parameter_set_rbsp(int width, int height, VdpDecoderProfile profile, VdpPictureInfoH264* sps, bs_t* b)
{
    int i;

    int profile_idc;
    switch (profile) {
        case VDP_DECODER_PROFILE_H264_BASELINE:
            profile_idc = H264_PROFILE_BASELINE;
            break;
        case VDP_DECODER_PROFILE_H264_MAIN:
            profile_idc = H264_PROFILE_MAIN;
            break;
        case VDP_DECODER_PROFILE_H264_HIGH:
        default:
            profile_idc = H264_PROFILE_HIGH;
            break;
    }
    bs_write_u8(b, profile_idc);
    bs_write_u1(b, 0);//sps->constraint_set0_flag);
    bs_write_u1(b, 0);//sps->constraint_set1_flag);
    bs_write_u1(b, 0);//sps->constraint_set2_flag);
    bs_write_u1(b, 0);//sps->constraint_set3_flag);
    bs_write_u1(b, 0);//sps->constraint_set4_flag);
    bs_write_u1(b, 0);//sps->constraint_set5_flag);
    bs_write_u(b, 2, 0);  /* reserved_zero_2bits */
    bs_write_u8(b, 31);//sps->level_idc);
    bs_write_ue(b, 0);//sps->seq_parameter_set_id);
    if( profile_idc == H264_PROFILE_HIGH )
    {
        bs_write_ue(b, 1);//sps->chroma_format_idc);
        //if( sps->chroma_format_idc == 3 )
        //{
        //    bs_write_u1(b, 0);//sps->residual_colour_transform_flag);
        //}
        bs_write_ue(b, 0);//sps->bit_depth_luma_minus8);
        bs_write_ue(b, 0);//sps->bit_depth_chroma_minus8);
        bs_write_u1(b, 0);//sps->qpprime_y_zero_transform_bypass_flag);
        bs_write_u1(b, 1);//sps->seq_scaling_matrix_present_flag);
        if( 1 )//sps->seq_scaling_matrix_present_flag )
        {
            for( i = 0; i < 8; i++ )
            {
                bs_write_u1(b, 1);//sps->seq_scaling_list_present_flag[ i ]);
                if( 1 )//sps->seq_scaling_list_present_flag[ i ] )
                {
                    if( i < 6 )
                    {
                        write_scaling_list( b, sps->scaling_lists_4x4[ i ], 16, 0);
                    }
                    else
                    {
                        write_scaling_list( b, sps->scaling_lists_8x8[ i - 6 ], 64, 0);
                    }
                }
            }
        }
    }
    bs_write_ue(b, sps->log2_max_frame_num_minus4);
    bs_write_ue(b, sps->pic_order_cnt_type);
    if( sps->pic_order_cnt_type == 0 )
    {
        bs_write_ue(b, sps->log2_max_pic_order_cnt_lsb_minus4);
    }
    else if( sps->pic_order_cnt_type == 1 )
    {
        bs_write_u1(b, sps->delta_pic_order_always_zero_flag);
        bs_write_se(b, 0);//sps->offset_for_non_ref_pic);
        bs_write_se(b, 0);//sps->offset_for_top_to_bottom_field);
        bs_write_ue(b, 0);//sps->num_ref_frames_in_pic_order_cnt_cycle);
        //for( i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++ )
        //{
        //    bs_write_se(b, sps->offset_for_ref_frame[ i ]);
        //}
    }
    bs_write_ue(b, sps->num_ref_frames);
    bs_write_u1(b, 0);//sps->gaps_in_frame_num_value_allowed_flag);
    bs_write_ue(b, (width - 1) / 16);//sps->pic_width_in_mbs_minus1);
    bs_write_ue(b, (height - 1) / 16);//sps->pic_height_in_map_units_minus1);
    bs_write_u1(b, sps->frame_mbs_only_flag);
    if( !sps->frame_mbs_only_flag )
    {
        bs_write_u1(b, sps->mb_adaptive_frame_field_flag);
    }
    bs_write_u1(b, sps->direct_8x8_inference_flag);
    int crop = width % 16 != 0 || height % 16 != 0;
    bs_write_u1(b, crop);//sps->frame_cropping_flag);
    if( crop )
    {
        bs_write_ue(b, 0);
        bs_write_ue(b, width%16);
        bs_write_ue(b, 0);
        bs_write_ue(b, height%16);
    }
    bs_write_u1(b, 0);//sps->vui_parameters_present_flag);
    //if( sps->vui_parameters_present_flag )
    //{
    //    write_vui_parameters(sps, b);
    //}
    write_rbsp_trailing_bits(b);
}

//7.3.2.1.1 Scaling list syntax
void write_scaling_list(bs_t* b, uint8_t* scalingList, int sizeOfScalingList, int useDefaultScalingMatrixFlag )
{
    int j;

    int lastScale = 8;
    int nextScale = 8;

    for( j = 0; j < sizeOfScalingList; j++ )
    {
        int delta_scale;

        if( nextScale != 0 )
        {
            // FIXME will not write in most compact way - could truncate list if all remaining elements are equal
            nextScale = scalingList[ j ];

            if (useDefaultScalingMatrixFlag)
            {
                nextScale = 0;
            }

            delta_scale = (nextScale - lastScale) % 256 ;
            bs_write_se(b, delta_scale);
        }

        lastScale = scalingList[ j ];
    }
}

//7.3.2.2 Picture parameter set RBSP syntax
void write_pic_parameter_set_rbsp(VdpPictureInfoH264* pps, bs_t* b)
{
    bs_write_ue(b, 0);//pps->pic_parameter_set_id);
    bs_write_ue(b, 0);//pps->seq_parameter_set_id);
    bs_write_u1(b, pps->entropy_coding_mode_flag);
    bs_write_u1(b, pps->pic_order_present_flag);
    bs_write_ue(b, 0);//pps->num_slice_groups_minus1);

    /*if( pps->num_slice_groups_minus1 > 0 )
    {
        int i_group;
        bs_write_ue(b, pps->slice_group_map_type);
        if( pps->slice_group_map_type == 0 )
        {
            for( i_group = 0; i_group <= pps->num_slice_groups_minus1; i_group++ )
            {
                bs_write_ue(b, pps->run_length_minus1[ i_group ]);
            }
        }
        else if( pps->slice_group_map_type == 2 )
        {
            for( i_group = 0; i_group < pps->num_slice_groups_minus1; i_group++ )
            {
                bs_write_ue(b, pps->top_left[ i_group ]);
                bs_write_ue(b, pps->bottom_right[ i_group ]);
            }
        }
        else if( pps->slice_group_map_type == 3 ||
                 pps->slice_group_map_type == 4 ||
                 pps->slice_group_map_type == 5 )
        {
            bs_write_u1(b, pps->slice_group_change_direction_flag);
            bs_write_ue(b, pps->slice_group_change_rate_minus1);
        }
        else if( pps->slice_group_map_type == 6 )
        {
            bs_write_ue(b, pps->pic_size_in_map_units_minus1);
            for( i = 0; i <= pps->pic_size_in_map_units_minus1; i++ )
            {
                bs_write_u(b, intlog2( pps->num_slice_groups_minus1 + 1 ), pps->slice_group_id[ i ] ); // was u(v)
            }
        }
    }*/
    bs_write_ue(b, pps->num_ref_idx_l0_active_minus1);
    bs_write_ue(b, pps->num_ref_idx_l1_active_minus1);
    bs_write_u1(b, pps->weighted_pred_flag);
    bs_write_u(b,2, pps->weighted_bipred_idc);
    bs_write_se(b, pps->pic_init_qp_minus26);
    bs_write_se(b, 0);//pps->pic_init_qs_minus26);
    bs_write_se(b, pps->chroma_qp_index_offset);
    bs_write_u1(b, pps->deblocking_filter_control_present_flag);
    bs_write_u1(b, pps->constrained_intra_pred_flag);
    bs_write_u1(b, pps->redundant_pic_cnt_present_flag);

    if ( 1 )//pps->_more_rbsp_data_present )
    {
        bs_write_u1(b, pps->transform_8x8_mode_flag);
        bs_write_u1(b, 0);//pps->pic_scaling_matrix_present_flag);
        /*if( pps->pic_scaling_matrix_present_flag )
        {
            int i;
            for( i = 0; i < 6 + 2* pps->transform_8x8_mode_flag; i++ )
            {
                bs_write_u1(b, pps->pic_scaling_list_present_flag[ i ]);
                if( pps->pic_scaling_list_present_flag[ i ] )
                {
                    if( i < 6 )
                    {
                        write_scaling_list( b, pps->ScalingList4x4[ i ], 16,
                                      pps->UseDefaultScalingMatrix4x4Flag[ i ] );
                    }
                    else
                    {
                        write_scaling_list( b, pps->ScalingList8x8[ i - 6 ], 64,
                                      pps->UseDefaultScalingMatrix8x8Flag[ i - 6 ] );
                    }
                }
            }
        }*/
        bs_write_se(b, pps->second_chroma_qp_index_offset);
    }

    write_rbsp_trailing_bits(b);
}

//7.3.2.11 RBSP trailing bits syntax
void write_rbsp_trailing_bits(bs_t* b)
{
    int rbsp_stop_one_bit = 1;
    int rbsp_alignment_zero_bit = 0;

    bs_write_f(b,1, rbsp_stop_one_bit); // equal to 1
    while( !bs_byte_aligned(b) )
    {
        bs_write_f(b,1, rbsp_alignment_zero_bit); // equal to 0
    }
}
