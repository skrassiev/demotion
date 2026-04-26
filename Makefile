# Standalone Makefile for motion_plugin.so
# Built for Raspberry Pi OS (Bookworm) arm64
# Uses system-installed libcamera and assumes rpicam-apps headers are available.

CXX = g++
CXXFLAGS = -std=c++2b -fPIC -O3 -Wall

# libcamera dependencies
LIBCAMERA_CFLAGS := $(shell pkg-config --cflags libcamera 2>/dev/null || echo -I/usr/include/libcamera)
LIBCAMERA_LIBS := $(shell pkg-config --libs libcamera 2>/dev/null || echo -lcamera -lcamera-base)

# rpicam-apps headers
# On a standard install, these might be in /usr/include/rpicam-apps
# If you have the rpicam-apps source elsewhere, override this on the command line:
# make RPICAM_APPS_INC=/path/to/rpicam-apps
RPICAM_APPS_INC ?= /usr/include/rpicam-apps

INCLUDES = -Isrc $(LIBCAMERA_CFLAGS) -I$(RPICAM_APPS_INC)

SOURCES = src/motion_plugin.cc src/background_subtractor.cc
TARGET = motion_plugin.so

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -shared -o $@ $(SOURCES) $(LIBCAMERA_LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
