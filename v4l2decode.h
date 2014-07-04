#include "parser.h"

typedef struct {
    int decoderHandle;

    int outputBuffersCount;
    int captureBuffersCount;

    v4l2_buffer_t *outputBuffers;
    v4l2_buffer_t *captureBuffers;

    int captureWidth;
    int captureHeight;

    int headerProcessed;
    parser_mode_t mode;
    __u32 codec;
    parser_context_t *parser;
} v4l2_decoder_t;

#define STREAM_BUFFER_SIZE        1048576 //compressed frame size. 1080p mpeg4 10Mb/s can be >256k in size, so this is to make sure frame fits into buffer
                                          //for very unknown reason lesser than 1Mb buffer causes MFC to corrupt its own setup setting inapropriate values

#define STREAM_BUFFER_CNT         2       //1 doesn't work at all

#define CAPTURE_EXTRA_BUFFER_CNT  1       //can be 1
