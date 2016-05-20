#include "vdpau_private.h"
#include "linux/videodev2.h"

#define kDPBMaxSize 16
#define kMaxVideoFrames 4
#define kPicsInPipeline (kMaxVideoFrames + 2)
#define kOutputBufferCnt (kPicsInPipeline + kDPBMaxSize)

int v4l2_init(const char *device_path);
int v4l2_init_by_name(const char *name);
int v4l2_deinit(decoder_ctx_t *dec);
int v4l2_reqbufs(decoder_ctx_t *dec);
int v4l2_querybuf(decoder_ctx_t *dec);
int v4l2_expbuf(decoder_ctx_t *dec);
int v4l2_s_fmt_input(decoder_ctx_t *dec);
int v4l2_s_fmt_output(decoder_ctx_t *dec);
int v4l2_streamon(decoder_ctx_t *dec);
int v4l2_streamoff(decoder_ctx_t *dec);
int v4l2_s_ext_ctrls(decoder_ctx_t *dec,
		struct v4l2_ext_controls* ext_ctrls);
int v4l2_qbuf_input(decoder_ctx_t *dec);
int v4l2_qbuf_output(decoder_ctx_t *dec, int index);
int v4l2_dqbuf_input(decoder_ctx_t *dec);
int v4l2_dqbuf_output(decoder_ctx_t *dec);
