/*
 * V4L2 Codec decoding example application
 * Kamil Debski <k.debski@samsung.com>
 *
 * Really simple stream parser header file
 *
 * Copyright 2012 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef INCLUDE_PARSER_H
#define INCLUDE_PARSER_H

#include <stdint.h>

/* parser mode */
typedef enum {
    MODE_H264,
    MODE_MPEG4,
    MODE_MPEG12,
} parser_mode_t;

/* parser states */
typedef enum {
    PARSER_NO_CODE,
    PARSER_CODE_0x1,
    PARSER_CODE_0x2,
    PARSER_CODE_0x3,
    PARSER_CODE_1x1,
    PARSER_CODE_SLICE,
} parser_state_t;

/* recent tag type */
typedef enum {
    TAG_HEAD,
    TAG_PICTURE,
} parser_tag_type_t;

/* Parser context */
typedef struct {
    parser_mode_t mode;
    parser_state_t state;
    parser_tag_type_t last_tag;

    int main_count;
    int headers_count;
    int tmp_code_start;
    int code_start;
    int code_end;
    char got_start;
    char got_end;
    char seek_end;
    int short_header;

    uint8_t *buffer;
    uint32_t head;
    uint32_t tail;
    uint32_t ptr;
    uint32_t size;
} parser_context_t;

/* Initialize the stream parser */
parser_context_t *parse_stream_init(parser_mode_t mode, uint32_t max_buffer_size);

/* Push data into the stream for parsing */
int parse_push(parser_context_t *ctx, const uint8_t* in, const uint32_t in_size);

/* Parser the stream:
 * - consumed is used to return the number of bytes consumed from the output
 * - frame_size is used to return the size of the frame that has been extracted
 * - get_head - when equal to 1 it is used to extract the stream header wehn
 *   setting up MFC
 * Return value: 1 - if a complete frame has been extracted, 0 otherwise
 */
int parse_stream(parser_context_t *ctx, char* out, int out_size, int *frame_size, char get_head);

#endif /* PARSER_H_ */

