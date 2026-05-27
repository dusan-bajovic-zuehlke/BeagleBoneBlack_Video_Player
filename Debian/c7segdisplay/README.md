# Run video and clock on striped Debian on BegaleBone Black

This directory contains a script and program to play chosen video and clock on display from BeagleBone Black.

## Contents

```
c7segdisplay/
├── starthere.sh ← script to start everything
├── README.md    ← this file
└── ...          ← other files
```


## Usage

### 1. Paste the c7segdisplay  contents into root
```bash
scp -r /c7segdisplay root@192.168.7.2:/root/
```

### 2. Paste a `video.mp4` in the same place
```bash
scp video.mp4 root@192.168.7.2:/root/
```

### 3. Run the starthere script
```bash
source ./starthere.sh
```

### 4. Use SSH to control BeagleBone Black
Over SSH, run:
```bash
sudo killall mp4player seg7
``` 

to shut down the displays
