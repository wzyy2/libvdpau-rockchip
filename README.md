# Introduction

This is an experimental VDPAU implementation for ROCKCHIP SoCs.

It only supports H264 video now.

Installation:

   $ make
   $ make install

Usage:

   $ export VDPAU_DRIVER=rockchip
   $ mpv --vo=vdpau --hwdec=vdpau --hwdec-codecs=all [filename]

Note:

This depends on rockchip h264 decode library(which is librkdec-h264d.so), and rockchip's v4l2 video driver(rk3288 & rk3399).
