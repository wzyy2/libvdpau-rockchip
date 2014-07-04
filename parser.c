#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "parser.h"

parser_context_t *parse_stream_init(parser_mode_t mode, uint32_t max_buffer_size)
{
    parser_context_t *ctx = calloc(1, sizeof(parser_context_t));
    ctx->buffer = malloc(max_buffer_size);
    if (ctx->buffer == NULL) {
        free(ctx);
        return NULL;
    }
    ctx->mode = mode;
    ctx->size = max_buffer_size;
    ctx->head = 0;
    ctx->tail = 0;
    ctx->ptr = 0;
    return ctx;
}

int parse_push(parser_context_t *ctx, const uint8_t* in, const uint32_t in_size)
{
    if(ctx->head - ctx->tail + in_size > ctx->size) {
        return -1;
    }
    uint32_t offset = ctx->head % ctx->size;
    if (offset + in_size <= ctx->size) {
        memcpy(ctx->buffer + offset, in, in_size);
    } else {
        int first = ctx->size - offset;
        memcpy(ctx->buffer + offset, in, first);
        memcpy(ctx->buffer, in + first, in_size - first);
    }
    ctx->head += in_size;
    return 0;
}

static inline uint8_t peek(parser_context_t *ctx) {
    return *(ctx->buffer + (ctx->ptr % ctx->size));
}

int parse_stream(parser_context_t *ctx, char* out, int out_size, int *frame_size, char get_head)
{
    uint32_t in_size = ctx->head - ctx->ptr;
    char frame_finished;
    int frame_length;

    frame_finished = 0;

    while (in_size-- > 0) {
        uint8_t inByte = peek(ctx);
        switch (ctx->state) {
        case PARSER_NO_CODE:
            if (inByte == 0x0) {
                ctx->state = PARSER_CODE_0x1;
                ctx->tmp_code_start = ctx->ptr;
            }
            break;
        case PARSER_CODE_0x1:
            if (inByte == 0x0)
                ctx->state = PARSER_CODE_0x2;
            else
                ctx->state = PARSER_NO_CODE;
            break;
        case PARSER_CODE_0x2:
            if (inByte == 0x1) {
                ctx->state = PARSER_CODE_1x1;
            } else if (ctx->mode == MODE_MPEG4 && (inByte & 0xFC) == 0x80) {
                /* Short header */
                ctx->state = PARSER_NO_CODE;
                /* Ignore the short header if the current hasn't
                 * been started with a short header. */
                if (get_head && !ctx->short_header) {
                    ctx->last_tag = TAG_HEAD;
                    ctx->headers_count++;
                    ctx->short_header = 1;
                } else if (!ctx->seek_end ||
                    (ctx->seek_end && ctx->short_header)) {
                    ctx->last_tag = TAG_PICTURE;
                    ctx->main_count++;
                    ctx->short_header = 1;
                }
            } else if (inByte == 0x0) {
                if (ctx->mode == MODE_H264) {
                    ctx->state = PARSER_CODE_0x3;
                } else {
                    ctx->tmp_code_start++;
                }
            } else {
                ctx->state = PARSER_NO_CODE;
            }
            break;
        case PARSER_CODE_0x3:
            if (inByte == 0x1)
                ctx->state = PARSER_CODE_1x1;
            else if (inByte == 0x0)
                ctx->tmp_code_start++;
            else
                ctx->state = PARSER_NO_CODE;
            break;
        case PARSER_CODE_1x1:
            if (ctx->mode == MODE_H264 && ((inByte & 0x1F) == 1 || (inByte & 0x1F) == 5)) {
                ctx->state = PARSER_CODE_SLICE;
            } else if ((ctx->mode == MODE_H264 && ((inByte & 0x1F) == 6 || (inByte & 0x1F) == 7 || (inByte & 0x1F) == 8))
                    || (ctx->mode == MODE_MPEG4 && ((inByte & 0xF0) <= 0x20 || inByte == 0xb0 || inByte == 0xb2 || inByte == 0xb3 || inByte == 0xb5))
                    || (ctx->mode == MODE_MPEG12 && (inByte == 0xb3 || inByte == 0xb8)) ) {
                ctx->state = PARSER_NO_CODE;
                ctx->last_tag = TAG_HEAD;
                ctx->headers_count++;
            } else if ((ctx->mode == MODE_MPEG4 && inByte == 0xb6)
                    || (ctx->mode == MODE_MPEG12 && inByte == 0x00) ) {
                ctx->state = PARSER_NO_CODE;
                ctx->last_tag = TAG_PICTURE;
                ctx->main_count++;
            } else
                ctx->state = PARSER_NO_CODE;
            break;
        case PARSER_CODE_SLICE:
            if ((inByte & 0x80) == 0x80) {
                ctx->main_count++;
                ctx->last_tag = TAG_PICTURE;
            }
            ctx->state = PARSER_NO_CODE;
            break;
        }

        if (get_head == 1 && ctx->headers_count >= 1 && ctx->main_count == 1) {
            ctx->code_end = ctx->tmp_code_start;
            ctx->got_end = 1;
            break;
        }

        if (ctx->got_start == 0 && ctx->headers_count == 1 && ctx->main_count == 0) {
            ctx->code_start = ctx->tmp_code_start;
            ctx->got_start = 1;
        }

        if (ctx->got_start == 0 && ctx->headers_count == 0 && ctx->main_count == 1) {
            ctx->code_start = ctx->tmp_code_start;
            ctx->got_start = 1;
            ctx->seek_end = 1;
            ctx->main_count = 0;
        }

        if (ctx->seek_end == 0 && ctx->headers_count > 0 && ctx->main_count == 1) {
            ctx->seek_end = 1;
            ctx->headers_count = 0;
            ctx->main_count = 0;
        }

        if (ctx->seek_end == 1 && (ctx->headers_count > 0 || ctx->main_count > 0)) {
            ctx->code_end = ctx->tmp_code_start;
            ctx->got_end = 1;
            if (ctx->headers_count == 0)
                ctx->seek_end = 1;
            else
                ctx->seek_end = 0;
            break;
        }

        ctx->ptr++;
    }

    if (ctx->got_end && ctx->got_start) {
        frame_length = ctx->code_end - ctx->code_start;
        if (out_size < frame_length) {
            //err("Output buffer too small for current frame %d", frame_length);
            return -1;
        }

        if ((ctx->code_start % ctx->size) + frame_length > ctx->size) {
            int overrun = (ctx->code_start + frame_length) % ctx->size;
            memcpy(out, ctx->buffer + (ctx->code_start % ctx->size), frame_length-overrun);
            memcpy(out+frame_length-overrun, ctx->buffer, overrun);
        } else
            memcpy(out, ctx->buffer + (ctx->code_start % ctx->size), frame_length);
        *frame_size = frame_length;
        ctx->tail = ctx->code_end;

        ctx->code_start = ctx->code_end;
        ctx->got_start = 1;
        ctx->got_end = 0;
        frame_finished = 1;
        if (ctx->last_tag == TAG_PICTURE) {
            ctx->seek_end = 1;
            ctx->main_count = 0;
            ctx->headers_count = 0;
        } else {
            ctx->seek_end = 0;
            ctx->main_count = 0;
            ctx->headers_count = 1;
            ctx->short_header = 0;
        }
    }

    ctx->tmp_code_start = ctx->ptr;

    return frame_finished;
}
