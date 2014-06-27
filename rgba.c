/*
 * Copyright (c) 2013-2014 Jens Kuske <jenskuske@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <string.h>
#include <sys/ioctl.h>
#include "vdpau_private.h"
#include "rgba.h"

static void dirty_add_rect(VdpRect *dirty, const VdpRect *rect)
{
	dirty->x0 = min(dirty->x0, rect->x0);
	dirty->y0 = min(dirty->y0, rect->y0);
	dirty->x1 = max(dirty->x1, rect->x1);
	dirty->y1 = max(dirty->y1, rect->y1);
}

static int dirty_in_rect(const VdpRect *dirty, const VdpRect *rect)
{
	return (dirty->x0 >= rect->x0) && (dirty->y0 >= rect->y0) &&
	       (dirty->x1 <= rect->x1) && (dirty->y1 <= rect->y1);
}

static VdpRect rgba_clip(rgba_surface_t *rgba, const VdpRect *rect) {
	VdpRect d_rect = {0, 0, rgba->width, rgba->height};
	if (rect) {
		d_rect.x0 = max(rect->x0, 0);
		d_rect.y0 = max(rect->y0, 0);
		d_rect.x1 = min(rect->x1, rgba->width);
		d_rect.y1 = min(rect->y1, rgba->height);
	}
	return d_rect;
}

VdpStatus rgba_create(rgba_surface_t *rgba,
                      device_ctx_t *device,
                      uint32_t width,
                      uint32_t height,
                      VdpRGBAFormat format)
{
	if (format != VDP_RGBA_FORMAT_B8G8R8A8 && format != VDP_RGBA_FORMAT_R8G8B8A8)
		return VDP_STATUS_INVALID_RGBA_FORMAT;

	if (width < 1 || width > 8192 || height < 1 || height > 8192)
		return VDP_STATUS_INVALID_SIZE;

	rgba->device = device;
	rgba->width = width;
	rgba->height = height;
	rgba->format = format;

	rgba->data = malloc(width * height * 4);
	if (!rgba->data)
		return VDP_STATUS_RESOURCES;

	rgba->dirty.x0 = width;
	rgba->dirty.y0 = height;
	rgba->dirty.x1 = 0;
	rgba->dirty.y1 = 0;
	rgba_fill(rgba, NULL, 0x00000000);

	return VDP_STATUS_OK;
}

void rgba_destroy(rgba_surface_t *rgba)
{
	free(rgba->data);
}

VdpStatus rgba_put_bits_native(rgba_surface_t *rgba,
                               void const *const *source_data,
                               uint32_t const *source_pitches,
                               VdpRect const *destination_rect)
{
	VdpRect d_rect = rgba_clip(rgba, destination_rect);

	if ((rgba->flags & RGBA_FLAG_NEEDS_CLEAR) && !dirty_in_rect(&rgba->dirty, &d_rect))
		rgba_clear(rgba);

	if (0 == d_rect.x0 && rgba->width == d_rect.x1 && source_pitches[0] == d_rect.x1) {
		// full width
		const int bytes_to_copy =
			(d_rect.x1 - d_rect.x0) * (d_rect.y1 - d_rect.y0) * 4;
		memcpy(rgba->data + d_rect.y0 * rgba->width * 4,
			   source_data[0], bytes_to_copy);
	} else {
		const unsigned int bytes_in_line = (d_rect.x1-d_rect.x0) * 4;
		unsigned int y;
		for (y = d_rect.y0; y < d_rect.y1; y ++) {
			memcpy(rgba->data + (y * rgba->width + d_rect.x0) * 4,
				   source_data[0] + (y - d_rect.y0) * source_pitches[0],
				   bytes_in_line);
		}
	}

	rgba->flags &= ~RGBA_FLAG_NEEDS_CLEAR;
	rgba->flags |= RGBA_FLAG_DIRTY;
	dirty_add_rect(&rgba->dirty, &d_rect);

	return VDP_STATUS_OK;
}

VdpStatus rgba_put_bits_indexed(rgba_surface_t *rgba,
                                VdpIndexedFormat source_indexed_format,
                                void const *const *source_data,
                                uint32_t const *source_pitch,
                                VdpRect const *destination_rect,
                                VdpColorTableFormat color_table_format,
                                void const *color_table)
{
	if (color_table_format != VDP_COLOR_TABLE_FORMAT_B8G8R8X8)
		return VDP_STATUS_INVALID_COLOR_TABLE_FORMAT;

	int x, y;
	const uint32_t *colormap = color_table;
	const uint8_t *src_ptr = source_data[0];
	uint32_t *dst_ptr = rgba->data;

	VdpRect d_rect = rgba_clip(rgba, destination_rect);

	if ((rgba->flags & RGBA_FLAG_NEEDS_CLEAR) && !dirty_in_rect(&rgba->dirty, &d_rect))
		rgba_clear(rgba);

	dst_ptr += d_rect.y0 * rgba->width;
	dst_ptr += d_rect.x0;

	for (y = 0; y < d_rect.y1 - d_rect.y0; y++)
	{
		for (x = 0; x < d_rect.x1 - d_rect.x0; x++)
		{
			uint8_t i, a;
			switch (source_indexed_format)
			{
			case VDP_INDEXED_FORMAT_I8A8:
				i = src_ptr[x * 2];
				a = src_ptr[x * 2 + 1];
				break;
			case VDP_INDEXED_FORMAT_A8I8:
				a = src_ptr[x * 2];
				i = src_ptr[x * 2 + 1];
				break;
			default:
				return VDP_STATUS_INVALID_INDEXED_FORMAT;
			}
			// TODO if rgba->format == VDP_RGBA_FORMAT_R8G8B8A8 then swap!
			dst_ptr[x] = (colormap[i] & 0x00ffffff) | (a << 24);
		}
		src_ptr += source_pitch[0];
		dst_ptr += rgba->width;
	}

	rgba->flags &= ~RGBA_FLAG_NEEDS_CLEAR;
	rgba->flags |= RGBA_FLAG_DIRTY;
	dirty_add_rect(&rgba->dirty, &d_rect);

	return VDP_STATUS_OK;
}

VdpStatus rgba_render_surface(rgba_surface_t *dest,
                              VdpRect const *destination_rect,
                              rgba_surface_t *src,
                              VdpRect const *source_rect,
                              VdpColor const *colors,
                              VdpOutputSurfaceRenderBlendState const *blend_state,
                              uint32_t flags)
{
	if (colors || flags)
		VDPAU_DBG_ONCE("%s: colors and flags not implemented!", __func__);

	// set up source/destination rects using defaults where required
	VdpRect s_rect = {0, 0, 0, 0};
	VdpRect d_rect = {0, 0, dest->width, dest->height};
	s_rect.x1 = src ? src->width : 1;
	s_rect.y1 = src ? src->height : 1;

	if (source_rect)
		s_rect = rgba_clip(src, source_rect);
	if (destination_rect)
		d_rect = rgba_clip(dest, destination_rect);

	// ignore zero-sized surfaces (also workaround for g2d driver bug)
	if (s_rect.x0 == s_rect.x1 || s_rect.y0 == s_rect.y1 ||
	    d_rect.x0 == d_rect.x1 || d_rect.y0 == d_rect.y1)
		return VDP_STATUS_OK;

	if ((dest->flags & RGBA_FLAG_NEEDS_CLEAR) && !dirty_in_rect(&dest->dirty, &d_rect))
		rgba_clear(dest);

	if (!src)
		rgba_fill(dest, &d_rect, 0xffffffff);
	else
		rgba_blit(dest, &d_rect, src, &s_rect);

	dest->flags &= ~RGBA_FLAG_NEEDS_CLEAR;
	dest->flags |= RGBA_FLAG_DIRTY;
	dirty_add_rect(&dest->dirty, &d_rect);

	return VDP_STATUS_OK;
}

void rgba_clear(rgba_surface_t *rgba)
{
	if (!(rgba->flags & RGBA_FLAG_DIRTY))
		return;

	rgba_fill(rgba, &rgba->dirty, 0x00000000);
	rgba->flags &= ~(RGBA_FLAG_DIRTY || RGBA_FLAG_NEEDS_CLEAR);
	rgba->dirty.x0 = rgba->width;
	rgba->dirty.y0 = rgba->height;
	rgba->dirty.x1 = 0;
	rgba->dirty.y1 = 0;
}

void rgba_fill(rgba_surface_t *dest, const VdpRect *dest_rect, uint32_t color)
{
	int x, y, w, h, i;
	if (dest_rect) {
		x = dest_rect->x0;
		y = dest_rect->y0;
		w = min(dest_rect->x1 - dest_rect->x0, dest->width);
		h = min(dest_rect->y1 - dest_rect->y0, dest->height);
	} else {
		x = 0;
		y = 0;
		w = dest->width;
		h = dest->height;
	}

	if(x == 0 && y == 0 && w == dest->width && h == dest->height) {
		memset(dest->data, color, dest->width * dest->height * 4);
	} else {
		for(i=y ; i<y+h ; i++) {
			memset(dest->data + (i*dest->width + x)*4, color, w*4);
		}
	}
}


#define DUFFS_LOOP4(pixel_copy_increment, width)                        \
{ int n = (width+3)/4;                                                  \
	switch (width & 3) {                                                \
	case 0: do {    pixel_copy_increment;                               \
	case 3:     pixel_copy_increment;                                   \
	case 2:     pixel_copy_increment;                                   \
	case 1:     pixel_copy_increment;                                   \
		} while (--n > 0);                                              \
	}                                                                   \
}

/* fast ARGB888->(A)RGB888 blending with pixel alpha */
void rgba_blit(rgba_surface_t *dest, const VdpRect *dest_rect, rgba_surface_t *src, const VdpRect *src_rect) {
	int width = src_rect->x1 - src_rect->x0;
	int height = src_rect->y1 - src_rect->y0;

	/* Clip to dest area */
	height = height + dest_rect->y0 > dest->height ? dest->height - dest_rect->y0 : height;
	width = width + dest_rect->x0 > dest->width ? dest->width - dest_rect->x0 : width;
	
	uint32_t *srcp = (uint32_t *) src->data + src_rect->x0 + src_rect->y0 * src->width;
	int srcskip = src->width - width;

	uint32_t *dstp = (uint32_t *) dest->data + dest_rect->x0 + dest_rect->y0 * dest->width;
	int dstskip = dest->width - width;

	if (dest->flags & RGBA_FLAG_NEEDS_CLEAR) {
		while(height--) {
			memcpy(dstp, srcp, width*4);
			dstp += dest->width;
			srcp += src->width;
		}
		return;
	}
	
	while (--height) {
		DUFFS_LOOP4({
			uint32_t dalpha;
			uint32_t d;
			uint32_t s1;
			uint32_t d1;
			uint32_t s = *srcp;
			uint32_t alpha = s >> 24;
			if (alpha) {
			  if (alpha == 0xFF) {
				*dstp = *srcp;
			  } else {
				/*
				 * take out the middle component (green), and process
				 * the other two in parallel. One multiply less.
				 */
				d = *dstp;
				dalpha = d >> 24;
				s1 = s & 0xff00ff;
				d1 = d & 0xff00ff;
				d1 = (d1 + ((s1 - d1) * alpha >> 8)) & 0xff00ff;
				s &= 0xff00;
				d &= 0xff00;
				d = (d + ((s - d) * alpha >> 8)) & 0xff00;
				dalpha = alpha + (dalpha * (alpha ^ 0xFF) >> 8);
				*dstp = d1 | d | (dalpha << 24);
			  }
			}
			++srcp;
			++dstp;
		}, width);
		srcp += srcskip;
		dstp += dstskip;
	}
}

