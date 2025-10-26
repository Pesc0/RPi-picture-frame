# RPi-picture-frame
An immich-connected slideshow running on the RPi 1B 256M
Made of the following modules:

## proxy
Gets images from immich, scales them, adds date and place in the bottom left corner of the image with a light vignette to make the text readable.
Exposes an API to retrieve the processed images. 
This is done because the raspberry is too slow for image processing, so this part of the code can run on the server hosting immich.

## Downloader
Runs on the RPi, periodically gets the image list from the proxy and syncs them on the raspberry local file system.
In the final design the SD card is made read only by an overlayfs and the image storage is put on an external usb drive, so that it can die without losing the whole setup.

## Slideshow
Monitors a folder for jpeg files, shows them in order with a fade in between. 
It is possible to navigate images using keyboard arrows, and to stop auto advance by pressing space.
In the final design buttons on the GPIOs are mapped to the keyboard inputs.

---

Instructions for every module are in the respective folders README.


