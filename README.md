# Introduction

This is an experimental VDPAU implementation for ODROID SoCs.

Most features of full VDPAU are missing, only software-decoded videos
will work.

Only tested with mythtv where the renderer is VDPAU and the decoder is
ffmpeg.

   $ make
   $ make install

   $ export VDPAU_DRIVER=odroid

# Environment Variables

## VDPAU_DEBUG

This environment variable if set specifices debugging output options
it is specified as a comma seperated list of options. The options are as follows.

* `dump` the first 16 bytes of the data that will be passed to the MFC decoder is printed in HEX
* `raw` the raw bytes that will be passed to the MFC decoder are written to the file `vid.raw`

## Decoder Output PIX Formats

VM12 (4:2:0 2 Planes 16x16 Tiles) V4L2_PIX_FMT_NV12MT_16X16
TM12 (4:2:0 2 Planes 64x32 Tiles) V4L2_PIX_FMT_NV12MT
NM12 (4:2:0 2 Planes Y/CbCr)      V4L2_PIX_FMT_NV12M
NM21 (4:2:0 2 Planes Y/CrCb)      V4L2_PIX_FMT_NV21M
