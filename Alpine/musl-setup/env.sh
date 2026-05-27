#!/bin/bash

export CROSS=arm-linux-musleabihf
export CC=$CROSS-gcc
export AR=$CROSS-ar
export RANLIB=$CROSS-ranlib
export STRIP=$CROSS-strip
export PREFIX=$HOME/ffmpeg-armv7-musl-libs

export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig

export PATH=$HOME/arm-linux-musleabihf-cross/bin:$PATH
