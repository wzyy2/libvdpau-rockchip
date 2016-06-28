TARGET = libvdpau_rockchip.so.1
SRC = device.c presentation_queue.c surface_output.c surface_video.c \
      surface_bitmap.c video_mixer.c decoder.c handles.c \
      rgba.c gles.c h264_decoder.c \
      v4l2.c

CFLAGS ?= -Wall -O3 -g -I ./include -I/usr/include/libdrm
LDFLAGS ?=
LIBS ?= -lrt -lm -lX11 -lrkdec-h264d -ldrm -lEGL -lGLESv2
CC = $(CROSS_COMPILER)gcc

MAKEFLAGS += -rR --no-print-directory

DEP_CFLAGS ?= -MD -MP -MQ $@
LIB_CFLAGS ?= -fpic
LIB_LDFLAGS = -shared -Wl,-soname,$(TARGET)

OBJ = $(addsuffix .o,$(basename $(SRC)))
DEP = $(addsuffix .d,$(basename $(SRC)))

MODULEDIR = $(shell pkg-config --variable=moduledir vdpau)

ifeq ($(MODULEDIR),)
MODULEDIR=/usr/lib/arm-linux-gnueabihf/vdpau
endif

.PHONY: clean all install

all: $(TARGET)
$(TARGET): $(OBJ)
	$(CC) $(LIB_LDFLAGS) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

clean:
	rm -f $(OBJ)
	rm -f $(DEP)
	rm -f $(TARGET)

install: $(TARGET)
	install -D $(TARGET) $(DESTDIR)$(MODULEDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(MODULEDIR)/$(TARGET)

%.o: %.c
	$(CC) $(DEP_CFLAGS) $(LIB_CFLAGS) $(CFLAGS) $(LDFLAGS) -c $< -o $@

include $(wildcard $(DEP))
