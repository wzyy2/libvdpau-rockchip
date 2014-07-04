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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <linux/media.h>

#include "v4l2.h"
#include "vdpau_private.h"

int RequestBuffer(int device, enum v4l2_buf_type type, enum v4l2_memory memory, int numBuffers)
{
  struct v4l2_requestbuffers reqbuf;
  int ret = 0;

  if(device < 0)
    return FALSE;

  memzero(reqbuf);

  reqbuf.type     = type;
  reqbuf.memory   = memory;
  reqbuf.count    = numBuffers;

  ret = ioctl(device, VIDIOC_REQBUFS, &reqbuf);
  if (ret)
  {
    VDPAU_DBG("request buffers");
    return V4L2_ERROR;
  }

  return reqbuf.count;
}

int StreamOn(int device, enum v4l2_buf_type type, int onoff)
{
  int ret = 0;
  enum v4l2_buf_type setType = type;

  if(device < 0)
    return FALSE;

  ret = ioctl(device, onoff, &setType);
  if(ret)
    return FALSE;

  return TRUE;
}

int MmapBuffers(int device, int count, v4l2_buffer_t *v4l2Buffers, enum v4l2_buf_type type, enum v4l2_memory memory, int queue)
{
  struct v4l2_buffer buf;
  struct v4l2_plane planes[V4L2_NUM_MAX_PLANES];
  int ret;
  int i, j;

  if(device < 0 || !v4l2Buffers || count == 0)
    return FALSE;

  for(i = 0; i < count; i++)
  {
    memzero(buf);
    memzero(planes);
    buf.type      = type;
    buf.memory    = memory;
    buf.index     = i;
    buf.m.planes  = planes;
    buf.length    = V4L2_NUM_MAX_PLANES;

    ret = ioctl(device, VIDIOC_QUERYBUF, &buf);
    if (ret)
    {
      VDPAU_DBG("query output buffer");
      return FALSE;
    }

    v4l2_buffer_t *buffer = &v4l2Buffers[i];

    buffer->iNumPlanes = 0;
    for (j = 0; j < V4L2_NUM_MAX_PLANES; j++)
    {
      //printf("%s::%s - plane %d %d size %d 0x%08x", i, j, buf.m.planes[j].length,
      //    buf.m.planes[j].m.userptr);
      buffer->iSize[j]       = buf.m.planes[j].length;
      buffer->iBytesUsed[j]  = buf.m.planes[j].bytesused;
      if(buffer->iSize[j])
      {
        buffer->cPlane[j] = mmap(NULL, buf.m.planes[j].length, PROT_READ | PROT_WRITE,
                       MAP_SHARED, device, buf.m.planes[j].m.mem_offset);
        if(buffer->cPlane[j] == MAP_FAILED)
        {
          VDPAU_DBG("mapping output buffer");
          return FALSE;
        }
        memset(buffer->cPlane[j], 0, buf.m.planes[j].length);
        buffer->iNumPlanes++;
      }
    }
    buffer->iIndex = i;

    if(queue)
    {
      ret = ioctl(device, VIDIOC_QBUF, &buf);
      if (ret)
      {
        VDPAU_DBG("queue output buffer");
        return FALSE;
      }
    }
  }

  return TRUE;
}

v4l2_buffer_t *FreeBuffers(int count, v4l2_buffer_t *v4l2Buffers)
{
  int i, j;

  if(v4l2Buffers != NULL)
  {
    for(i = 0; i < count; i++)
    {
      v4l2_buffer_t *buffer = &v4l2Buffers[i];

      for (j = 0; j < buffer->iNumPlanes; j++)
      {
        if(buffer->cPlane[j] && buffer->cPlane[j] != MAP_FAILED)
        {
          munmap(buffer->cPlane[j], buffer->iSize[j]);
        }
      }
    }
    free(v4l2Buffers);
  }
  return NULL;
}

int DequeueBuffer(int device, enum v4l2_buf_type type, enum v4l2_memory memory, int planes)
{
  struct v4l2_buffer vbuf;
  struct v4l2_plane  vplanes[V4L2_NUM_MAX_PLANES];
  int ret = 0;

  if(device < 0)
    return V4L2_ERROR;

  memzero(vplanes);
  memzero(vbuf);
  vbuf.type     = type;
  vbuf.memory   = memory;
  vbuf.m.planes = vplanes;
  vbuf.length   = planes;

  ret = ioctl(device, VIDIOC_DQBUF, &vbuf);
  if (ret) {
    if (errno == EAGAIN)
      return -EAGAIN;
    VDPAU_DBG("dequeue input buffer");
    return V4L2_ERROR;
  }

  return vbuf.index;
}

int QueueBuffer(int device, enum v4l2_buf_type type,
    enum v4l2_memory memory, int planes, int index, v4l2_buffer_t *buffer)
{
  struct v4l2_buffer vbuf;
  struct v4l2_plane  vplanes[V4L2_NUM_MAX_PLANES];
  int ret = 0;
  int i;

  if(!buffer || device <0)
    return V4L2_ERROR;

  memzero(vplanes);
  memzero(vbuf);
  vbuf.type     = type;
  vbuf.memory   = memory;
  vbuf.index    = index;
  vbuf.m.planes = vplanes;
  vbuf.length   = buffer->iNumPlanes;

  for (i = 0; i < buffer->iNumPlanes; i++)
  {
    vplanes[i].m.mem_offset = (unsigned long)buffer->cPlane[i];
    vplanes[i].m.userptr    = (unsigned long)buffer->cPlane[i];
    vplanes[i].m.fd         = (unsigned long)buffer->cPlane[i];
    vplanes[i].length       = buffer->iSize[i];
    vplanes[i].bytesused    = buffer->iBytesUsed[i];
  }

  ret = ioctl(device, VIDIOC_QBUF, &vbuf);
  if (ret)
  {
    VDPAU_DBG("queue input buffer");
    return V4L2_ERROR;
  }

  return index;
}

int PollInput(int device, int timeout)
{
  int ret = 0;
  struct pollfd p;
  p.fd = device;
  p.events = POLLIN | POLLERR;

  ret = poll(&p, 1, timeout);
  if (ret < 0)
  {
    VDPAU_DBG("polling input");
    return V4L2_ERROR;
  }
  else if (ret == 0)
  {
    return V4L2_BUSY;
  }

  return V4L2_READY;
}

int PollOutput(int device, int timeout)
{
  int ret = 0;
  struct pollfd p;
  p.fd = device;
  p.events = POLLOUT | POLLERR;

  ret = poll(&p, 1, timeout);
  if (ret < 0)
  {
    VDPAU_DBG("polling output");
    return V4L2_ERROR;
  }
  else if (ret == 0)
  {
    return V4L2_BUSY;
  }

  return V4L2_READY;
}
