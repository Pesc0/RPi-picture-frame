# RPi-picture-frame
An immich-connected slideshow running on the RPi 1B 256M

```
apt install cmake make gcc g++
```

# Using Broadcom drivers
```
git clone https://github.com/raspberrypi/userland
cd userland
./buildme
```

edit `/boot/config.txt`
```
gpu_mem_256=72
#dtoverlay=vc4-kms-v3d
```

export LD_LIBRARY_PATH=/opt/vc/lib:$LD_LIBRARY_PATH

# Using KMSDRM
https://www.downtowndougbrown.com/2024/06/playing-1080p-h-264-video-on-my-old-256-mb-raspberry-pi/

```
cp /boot/firmware/overlays/vc4-kms-v3d.dtbo /boot/firmware/overlays/vc4-kms-v3d-256mb.dtbo
```

edit `/boot/config.txt`
```
gpu_mem_256=16
dtoverlay=vc4-kms-v3d-256mb,cma-96
```


rm /etc/modprobe.d/dietpi-disable_vcsm.conf
rm /etc/modprobe.d/dietpi-disable_rpi_codec.conf
rm /etc/modprobe.d/dietpi-disable_rpi_camera.conf


```
git clone https://github.com/Pesc0/RPi-picture-frame --recurse-submodules
cd RPi-picture-frame/slideshow
#git submodule foreach git pull origin HEAD
mkdir build && cd build
cmake ..
cmake --build . -j4
./slideshow
```