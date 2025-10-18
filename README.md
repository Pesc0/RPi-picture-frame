# RPi-picture-frame
An immich-connected slideshow running on the RPi 1B 256M

```
apt install cmake make gcc g++
```

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


```
git clone https://github.com/Pesc0/RPi-picture-frame --recurse-submodules
cd RPi-picture-frame/slideshow
#git submodule foreach git pull origin HEAD
mkdir build && cd build
cmake ..
cmake --build . -j4
./slideshow
```