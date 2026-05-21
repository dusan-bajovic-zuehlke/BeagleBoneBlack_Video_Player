gcc main_lr.c -o mp4player \
  -O3 -march=native -funroll-loops -ffast-math \
  -pthread \
  $(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale)\
&&\
gcc seg7.c -o seg7 -O3