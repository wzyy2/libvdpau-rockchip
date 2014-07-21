#include "parser.h"

typedef struct {
    uint32_t width;
    uint32_t height;
    __u32 codec;

    int decoderHandle;
    int converterHandle;

    int needConvert;

    int outputBuffersCount;
    int captureBuffersCount;
    int converterBuffersCount;

    v4l2_buffer_t *outputBuffers;
    v4l2_buffer_t *captureBuffers;
    v4l2_buffer_t *converterBuffers;

    int captureWidth;
    int captureHeight;

    int headerProcessed;
    parser_context_t *parser;
    parser_mode_t mode;
} v4l2_decoder_t;

#define STREAM_BUFFER_SIZE        1572864 //compressed frame size. 1080p mpeg4 10Mb/s can be >256k in size, so this is to make sure frame fits into buffer
                                          //for very unknown reason lesser than 1Mb buffer causes MFC to corrupt its own setup setting inapropriate values

#define STREAM_BUFFER_CNT         3       //3 input buffers. 2 is enough almost for everything, but on some heavy videos 3 makes a difference

#define CAPTURE_EXTRA_BUFFER_CNT  1       //can be 1

#define CONVERTER_VIDEO_BUFFERS_CNT 3     //2 begins to be slow. maybe on video only, but not on convert.
