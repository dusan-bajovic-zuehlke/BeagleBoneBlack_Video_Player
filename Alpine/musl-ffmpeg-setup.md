# Musl cross-compiler and FFmpeg setup

## muslc libc setup
In order to cross-compile with musl libc, you need to install the necessary compiler (assuming you are positioned in `$HOME` directory):
```bash
wget https://musl.cc/arm-linux-musleabihf-cross.tgz
tar -xf arm-linux-musleabihf-cross.tgz
````

If you want it to be available globally, you must add line `source ~/env.sh` to your `~/.bashrc` and<br>
reload shell like `source ~/.bashrc` (assuming you put `env.sh` in your `$HOME` directory).<br> 
The `env.sh` script sets cross-compiler and other necessities for further FFmpeg building and program cross-compiling.

You can verify cross-compiler installation with:
```bash
arm-linux-musleabihf-gcc --version
```
> Notice: The `$PREFIX` variable and cross-compiler path are currently hardcoded;
> you can change these settings to your liking.

## FFmpeg libraries setup
Firstly, you need to clone the FFmpeg repository:
```bash
git clone https://git.ffmpeg.org/ffmpeg.git
cd ffmpeg
```

Then you need to configure and build it:
```bash
./configure \
    --arch=arm \
    --target-os=linux \
    --cross-prefix=$CROSS \
    --enable-cross-compile \
    --prefix=$PREFIX \
    --extra-cflags="-I$PREFIX/include" \
    --extra-ldflags="-L$PREFIX/lib" \
    --enable-static \
    --disable-shared

make -j$(nproc)
make install
```
Your FFmpeg libraries will be located in the path defined by `$PREFIX` variable from `env.sh`.

## Compiling C program to ARM executable
With everything installed, you can compile your program using the `compile.sh` script
which calls the previously installed cross-compiler and FFmpeg libraries. 

## Testing on VM
To test how program works on VM you can copy the program using SSH (copying test video files can be done the same way):
```bash
scp -P <port> <program_name> root@<ip_address|localhost>:/<path>
```

## Testing on BeagleBone Black
To test how program works on BeagleBone Black, you can:
- either transfer program (and video files) to your SD card directly
- copy them like in previous section  
