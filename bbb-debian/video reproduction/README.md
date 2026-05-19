# #14 Enable Luka's video reproduction on lightweight OS from BBB

## 1. Before playing the video install libraries:
```
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev

```
## 2. Compile code and run
```
gcc videoplayerr.c  -lavformat -lavcodec -lavutil -lswscale -lpthread

./a.out video.mp4

```


