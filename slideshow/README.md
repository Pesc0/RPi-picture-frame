
```
apt install cmake make gcc g++ libturbojpeg0-dev libjpeg62-turbo-dev gpiod libgpiod-dev
```

edit `/boot/config.txt`
```
dtoverlay=gpio-key,gpio=17,active_low=1,gpio_pull=up,label=left,keycode=105
dtoverlay=gpio-key,gpio=18,active_low=1,gpio_pull=up,label=right,keycode=106
dtoverlay=gpio-key,gpio=22,active_low=1,gpio_pull=up,label=space,keycode=57
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
gpu_mem_256=32
dtoverlay=vc4-kms-v3d-256mb,cma-96
```

# Hardware jpeg decode
Could not get it to work. I gave up due to limited time but consider this an exercise to train my "good enough"
With turbojpeg 1080p images take 1s to load when navigating with keyboard.

Anyway these files blacklist the kernel modules required to access the VC4 codec when using V4L2
rm /etc/modprobe.d/dietpi-disable_vcsm.conf
rm /etc/modprobe.d/dietpi-disable_rpi_codec.conf
rm /etc/modprobe.d/dietpi-disable_rpi_camera.conf


```
git clone https://github.com/Pesc0/RPi-picture-frame --recurse-submodules
cd RPi-picture-frame/slideshow
#git submodule foreach git pull origin HEAD
mkdir build && cd build
cmake ..
cmake --build . -j2
./slideshow
```

Dont forget to edit .env appropriately if using systemd file. Default image location is /tmp