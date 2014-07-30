#pragma once

/*
 *      Copyright (C) 2005-2012 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "linux/videodev2.h"

#define V4L2_ERROR -1
#define V4L2_BUSY  1
#define V4L2_READY 2
#define V4L2_OK    3

#define V4L2_NUM_MAX_PLANES 3

#define TRUE    1
#define FALSE   0

#define memzero(x) memset(&(x), 0, sizeof (x))

typedef struct
{
  int   iSize[V4L2_NUM_MAX_PLANES];
  int   iOffset[V4L2_NUM_MAX_PLANES];
  int   iBytesUsed[V4L2_NUM_MAX_PLANES];
  void  *cPlane[V4L2_NUM_MAX_PLANES];
  int   iNumPlanes;
  int   iIndex;
  int   bQueue;
} v4l2_buffer_t;

int RequestBuffer(int device, enum v4l2_buf_type type, enum v4l2_memory memory, int numBuffers);
int StreamOn(int device, enum v4l2_buf_type type, int onoff);
int MmapBuffers(int device, int count, v4l2_buffer_t *v4l2Buffers, enum v4l2_buf_type type, enum v4l2_memory memory, int queue);
v4l2_buffer_t *FreeBuffers(int count, v4l2_buffer_t *v4l2Buffers);

int DequeueBuffer(int device, enum v4l2_buf_type type, enum v4l2_memory memory);
int QueueBuffer(int device, enum v4l2_buf_type type, enum v4l2_memory memory, v4l2_buffer_t *buffer);

int PollInput(int device, int timeout);
int PollOutput(int device, int timeout);

static inline int v4l2_align(int v, int a) {
  return ((v + a - 1) / a) * a;
}
