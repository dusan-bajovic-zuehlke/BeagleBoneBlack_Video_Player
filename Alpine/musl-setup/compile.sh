#!/bin/bash

arm-linux-musleabihf-gcc \
  -o fbplayer main.c \
  -I$PREFIX/include \
  -L$PREFIX/lib \
  -static \
  -Wl,--start-group \
  -lavformat -lavcodec -lswscale -lswresample -lavutil \
  -latomic \
  -lm -lpthread \
  -Wl,--end-group