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
