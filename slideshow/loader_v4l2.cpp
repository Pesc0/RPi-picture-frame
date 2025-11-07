
#include <vector>
#include <string>
#include <SDL3/SDL.h>
#include <cstddef>


#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

// REFERENCES
// https://github.com/raspberrypi/linux/blob/rpi-6.1.y/drivers/staging/vc04_services/bcm2835-codec/bcm2835-v4l2-codec.c#L266
// https://github.com/kmdouglass/v4l2-examples/tree/master
// https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/dev-decoder.html
// https://www.linuxtv.org/downloads/v4l-dvb-apis/userspace-api/v4l/v4l2.html
// https://www.linuxtv.org/downloads/v4l-dvb-apis/userspace-api/v4l/mmap.html#example-mapping-buffers-in-the-multi-planar-api
// https://www.linuxtv.org/downloads/v4l-dvb-apis/userspace-api/v4l/buffer.html#v4l2-buf-flag-queued
// https://www.linuxtv.org/downloads/v4l-dvb-apis/userspace-api/v4l/vidioc-qbuf.html#vidioc-qbuf
// https://github.com/raspberrypi/linux/issues/3791
// https://forums.raspberrypi.com/viewtopic.php?t=356791


// Upload ABGR/ARGB mapped data as GL_RGBA; ABGR32 means memory order A B G R,
// on many GL implementations GL_RGBA + GL_UNSIGNED_BYTE will match ABGR32 on little-endian
// If colors are swapped, change format to GL_BGRA or adjust shader accordingly.
#define LOADER_GL_PIXEL_FORMAT GL_RGBA


#define FMT_OUT_NUM_PLANES 1
#define FMT_CAP_NUM_PLANES 1

static int v4l2_fd = -1;

struct Buffer_OUT {
    void *start[FMT_OUT_NUM_PLANES] = {0};
    size_t length[FMT_OUT_NUM_PLANES] = {0};
};

struct Buffer_CAP {
    void *start[FMT_CAP_NUM_PLANES] = {0};
    size_t length[FMT_CAP_NUM_PLANES] = {0};
};

// persistent mmap buffers & counts
static std::vector<Buffer_OUT> v4l2_out_mmap;     // compressed input (OUTPUT) buffers
static std::vector<Buffer_CAP> v4l2_cap_mmap;     // decoded output (CAPTURE) buffers 

//FIXME remove
// store the chosen/negotiated width/height for capture
//static int v4l2_cap_width = 0;
//static int v4l2_cap_height = 0;

// Some fourcc fallbacks if kernel headers differ
//#ifndef V4L2_PIX_FMT_ABGR32
//    #define V4L2_PIX_FMT_ABGR32 v4l2_fourcc('A','B','2','4')
//#endif
//#ifndef V4L2_PIX_FMT_JPEG
//    #define V4L2_PIX_FMT_JPEG v4l2_fourcc('J','P','E','G')
//#endif

bool _init_img_loader() {

    v4l2_fd = open("/dev/video10", O_RDWR | O_CLOEXEC);
    if (v4l2_fd < 0) {
        SDL_Log("Failed to open /dev/video10: %s", strerror(errno));
        return false;
    }

    // 1) Set INPUT (OUTPUT_MPLANE) 
    struct v4l2_format fmt_out = {0};
    fmt_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt_out.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt_out.fmt.pix_mp.num_planes = FMT_OUT_NUM_PLANES;
    fmt_out.fmt.pix_mp.width = 1920;
    fmt_out.fmt.pix_mp.height = 1080; 
    if (ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt_out) < 0) {
        SDL_Log("VIDIOC_S_FMT (input) failed: %s", strerror(errno));
        return false;
    }

    char format_code[5] = {0};
    strncpy(format_code, (char*)&fmt_out.fmt.pix_mp.pixelformat, 4);
    printf("Set format:\n"
	 " Width: %d\n"
	 " Height: %d\n"
	 " Pixel format: %s\n"
	 " Field: %d\n",
	 fmt_out.fmt.pix_mp.width,
	 fmt_out.fmt.pix_mp.height,
	 format_code,
	 fmt_out.fmt.pix_mp.field);

    // 2) Set OUTPUT (CAPTURE_MPLANE) 
    struct v4l2_format fmt_cap = {0};
    fmt_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt_cap.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_RGBA32;
    fmt_cap.fmt.pix_mp.num_planes = FMT_CAP_NUM_PLANES;
    fmt_out.fmt.pix_mp.width = 1920;
    fmt_out.fmt.pix_mp.height = 1088; //multiple of 32 //FIXME add crop //FIXME does not get set: stays at 32x32
    if (ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt_cap) < 0) {
        SDL_Log("VIDIOC_S_FMT (output) failed: %s", strerror(errno));
        return false;
    }

    strncpy(format_code, (char*)&fmt_cap.fmt.pix_mp.pixelformat, 4);
    printf("Set format:\n"
	 " Width: %d\n"
	 " Height: %d\n"
	 " Pixel format: %s\n"
	 " Field: %d\n",
	 fmt_cap.fmt.pix_mp.width,
	 fmt_cap.fmt.pix_mp.height,
	 format_code,
	 fmt_cap.fmt.pix_mp.field);

    // 3) Request MMAP buffers for input (output)
    struct v4l2_requestbuffers req_out = {0};
    req_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    req_out.memory = V4L2_MEMORY_MMAP;
    req_out.count = 4; // a few buffers for pipelining
    if (ioctl(v4l2_fd, VIDIOC_REQBUFS, &req_out) < 0) {
        SDL_Log("VIDIOC_REQBUFS (output) failed: %s", strerror(errno));
        return false;
    }

    v4l2_out_mmap.resize(req_out.count);

    for (int i = 0; i < v4l2_out_mmap.size(); ++i) {
        struct v4l2_buffer buf = {0};
        struct v4l2_plane planes[FMT_OUT_NUM_PLANES];
        memset(planes, 0, sizeof(planes));

        buf.type = req_out.type;
        buf.memory = req_out.memory;
        buf.index = i;

        /* length in struct v4l2_buffer in multi-planar API stores the size
         * of planes array. */
        buf.length = FMT_OUT_NUM_PLANES;
        buf.m.planes = planes;

        if (ioctl(v4l2_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            SDL_Log("VIDIOC_QUERYBUF (output) failed: %s", strerror(errno));
            return false;
        }

        /* Every plane has to be mapped separately */
        for (int j = 0; j < FMT_OUT_NUM_PLANES; j++) {
            v4l2_out_mmap[i].length[j] = buf.m.planes[j].length; /* remember for munmap() */

            v4l2_out_mmap[i].start[j] = mmap(NULL, buf.m.planes[j].length,
                    PROT_READ | PROT_WRITE, /* recommended */
                    MAP_SHARED,             /* recommended */
                    v4l2_fd, buf.m.planes[j].m.mem_offset);

            if (MAP_FAILED == v4l2_out_mmap[i].start[j]) {
                /* If you do not exit here you should unmap() and free()
                the buffers and planes mapped so far. */
                SDL_Log("mmap (output buf %d) failed: %s", i, strerror(errno));
                return false;
            }
        }
    }

    // 4) Request MMAP buffers for output (capture)
    struct v4l2_requestbuffers req_cap = {0};
    req_cap.count = 4;
    req_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req_cap.memory = V4L2_MEMORY_MMAP;
    if (ioctl(v4l2_fd, VIDIOC_REQBUFS, &req_cap) < 0) {
        SDL_Log("VIDIOC_REQBUFS (capture) failed: %s", strerror(errno));
        return false;
    }

    v4l2_cap_mmap.resize(req_cap.count);

    for (int i = 0; i < v4l2_cap_mmap.size(); ++i) {
        struct v4l2_buffer buf = {0};
        struct v4l2_plane planes[FMT_CAP_NUM_PLANES];
        memset(planes, 0, sizeof(planes));

        buf.type = req_cap.type;
        buf.memory = req_cap.memory;
        buf.index = i;

        /* length in struct v4l2_buffer in multi-planar API stores the size
         * of planes array. */
        buf.length = FMT_CAP_NUM_PLANES;
        buf.m.planes = planes;

        if (ioctl(v4l2_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            SDL_Log("VIDIOC_QUERYBUF (capture) failed: %s", strerror(errno));
            return false;
        }

        /* Every plane has to be mapped separately */
        for (int j = 0; j < FMT_CAP_NUM_PLANES; j++) {
            v4l2_cap_mmap[i].length[j] = buf.m.planes[j].length; /* remember for munmap() */

            v4l2_cap_mmap[i].start[j] = mmap(NULL, buf.m.planes[j].length,
                    PROT_READ | PROT_WRITE, /* recommended */
                    MAP_SHARED,             /* recommended */
                    v4l2_fd, buf.m.planes[j].m.mem_offset);

            if (MAP_FAILED == v4l2_cap_mmap[i].start[j]) {
                /* If you do not exit here you should unmap() and free()
                the buffers and planes mapped so far. */
                SDL_Log("mmap (capture buf %d) failed: %s", i, strerror(errno));
                return false;
            }
        }

        // queue all capture buffers initially (they will be filled by the driver)
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
            SDL_Log("VIDIOC_QBUF (initial capture qbuf %d) failed: %s", i, strerror(errno));
            return false;
        }
    }


    // --- Stream on
    int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (ioctl(v4l2_fd, VIDIOC_STREAMON, &type) < 0) {
        SDL_Log("VIDIOC_STREAMON output failed: %s", strerror(errno));
        return false;
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(v4l2_fd, VIDIOC_STREAMON, &type) < 0) {
        SDL_Log("VIDIOC_STREAMON capture failed: %s", strerror(errno));
        return false;
    }

    return true;
}



bool _load_image(unsigned char *&pixeldata_out, size_t &pixeldata_len_out, const std::vector<unsigned char> &filebuf_in, const std::string &path_in, int &width, int &height) {
    if (v4l2_fd < 0 || v4l2_out_mmap.empty() || v4l2_cap_mmap.empty()) {
        SDL_Log("v4l2 not initialized");
        return false;
    }

    //fill input (output_mplane) buffers with jpeg data
    size_t remaining = filebuf_in.size();
    int buf_idx = 0;
    while(remaining > 0 && buf_idx < (v4l2_out_mmap.size()*3)) {
        struct v4l2_buffer out_buf = {0};
        struct v4l2_plane out_planes[FMT_OUT_NUM_PLANES];
        memset(out_planes, 0, sizeof(out_planes));

        out_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        out_buf.memory = V4L2_MEMORY_MMAP;
        out_buf.length = FMT_OUT_NUM_PLANES;
        out_buf.m.planes = out_planes;
        out_buf.index = buf_idx % v4l2_out_mmap.size();

        if (ioctl(v4l2_fd, VIDIOC_QUERYBUF, &out_buf) < 0) {
            SDL_Log("VIDIOC_QUERYBUF (output) failed: %s", strerror(errno));
            return false;
        }

        if(!(out_buf.flags & V4L2_BUF_FLAG_QUEUED)) { //if buffer not queued

            //if done dequeue it to make it available
            if (out_buf.flags & V4L2_BUF_FLAG_DONE) {
                if(ioctl(v4l2_fd, VIDIOC_DQBUF, &out_buf) < 0) {
                    SDL_Log("cannot get empty input buffer: VIDIOC_DQBUF output failed: %s", strerror(errno));
                    return false;
                }
            }

            //fill planes
            for (int p = 0; p < FMT_OUT_NUM_PLANES; ++p) {
                size_t plane_size = v4l2_out_mmap[out_buf.index].length[p];
                size_t to_copy = remaining < plane_size ? remaining : plane_size;
                memcpy(v4l2_out_mmap[out_buf.index].start[p], filebuf_in.data() + (filebuf_in.size() - remaining), to_copy);
                out_buf.m.planes[p].bytesused = to_copy;
                out_buf.m.planes[p].length = plane_size;
                remaining -= to_copy;
            }

            if (ioctl(v4l2_fd, VIDIOC_QBUF, &out_buf) < 0) {
                SDL_Log("VIDIOC_QBUF (output) failed: %s", strerror(errno));
                return false;
            }
            #ifdef DEBUG
            else {
                SDL_Log("VIDIOC_QBUF (output) success");
            }
            #endif
        }

        buf_idx++;
    }

    if (remaining > 0) {
        SDL_Log("JPEG not fully uploaded.");
        return false;
    }

    struct v4l2_format cur_fmt = {0};
    cur_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(v4l2_fd, VIDIOC_G_FMT, &cur_fmt) < 0) {
        SDL_Log("VIDIOC_G_FMT (capture) failed: %s", strerror(errno));
        return false;
    }

    width = cur_fmt.fmt.pix_mp.width;
    height = cur_fmt.fmt.pix_mp.height;

    char format_code[5] = {0};
    strncpy(format_code, (char*)&cur_fmt.fmt.pix_mp.pixelformat, 4);
    printf("Set format:\n"
	 " Width: %d\n"
	 " Height: %d\n"
	 " Pixel format: %s\n"
	 " Field: %d\n",
	 cur_fmt.fmt.pix_mp.width, //FIXME does not report correct resolution
	 cur_fmt.fmt.pix_mp.height,
	 format_code,
	 cur_fmt.fmt.pix_mp.field);

//FIXME read capture buffer into pixeldata_out
/* 

    //dequeue done input (output_mplane) buffers
    for (int i = 0; i < v4l2_out_mmap.size(); ++i) {
        struct v4l2_buffer out_buf = {0};
        struct v4l2_plane out_planes[FMT_OUT_NUM_PLANES];
        memset(out_planes, 0, sizeof(out_planes));

        out_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        out_buf.memory = V4L2_MEMORY_MMAP;
        out_buf.length = FMT_OUT_NUM_PLANES;
        out_buf.m.planes = out_planes;
        out_buf.index = i;

        if (ioctl(v4l2_fd, VIDIOC_QUERYBUF, &out_buf) < 0) {
            SDL_Log("VIDIOC_QUERYBUF (output) failed: %s", strerror(errno));
            return false;
        }

        if(!(out_buf.flags & V4L2_BUF_FLAG_QUEUED) && (out_buf.flags & V4L2_BUF_FLAG_DONE)) {
            if(ioctl(v4l2_fd, VIDIOC_DQBUF, &out_buf) < 0) {
                SDL_Log("cannot get empty input buffer: VIDIOC_DQBUF output failed: %s", strerror(errno));
                return false;
            }
        }
    }




    // We'll use buffer index 0 for output (compressed) for simplicity, but we rotate through available buffers
    static int out_idx = 0;
    static int cap_idx = 0;

    // 1) Copy JPEG into next output mmap buffer
    int chosen_out = out_idx % v4l2_out_buf.size();
    if (filebuf_in.size() > v4l2_out_mmap_len[chosen_out]) {
        SDL_Log("JPEG size %zu exceeds mapped output buffer size %zu", filebuf_in.size(), v4l2_out_mmap_len[chosen_out]);
        return false;
    }
    // copy compressed JPEG to the mmap region
    memcpy(v4l2_out_mmap[chosen_out], filebuf_in.data(), filebuf_in.size());

    // Prepare and queue the OUTPUT buffer (compressed)
    struct v4l2_buffer outbuf = {};
    struct v4l2_plane outplanes[VIDEO_MAX_PLANES];
    memset(outplanes, 0, sizeof(outplanes));
    outbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    outbuf.memory = V4L2_MEMORY_MMAP;
    outbuf.index = chosen_out;
    outbuf.length = 1;
    outbuf.m.planes = outplanes;
    outbuf.m.planes[0].bytesused = (unsigned int)filebuf_in.size();
    outbuf.m.planes[0].length = (unsigned int)v4l2_out_mmap_len[chosen_out];

    if (ioctl(v4l2_fd, VIDIOC_QBUF, &outbuf) < 0) {
        SDL_Log("VIDIOC_QBUF (output) failed: %s", strerror(errno));
        return false;
    }



    // 2) Prepare capture buffer (we use whichever is next; capture buffers were pre-queued in init,
    //    but to be safe we will dequeue one and requeue after processing)
    struct v4l2_buffer capbuf = {};
    struct v4l2_plane capplanes[VIDEO_MAX_PLANES];
    memset(capplanes, 0, sizeof(capplanes));
    capbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    capbuf.memory = V4L2_MEMORY_MMAP;
    capbuf.length = 1;
    capbuf.m.planes = capplanes;

    // Dequeue a decoded capture buffer (this blocks until decode completes)
    if (ioctl(v4l2_fd, VIDIOC_DQBUF, &capbuf) < 0) {
        SDL_Log("VIDIOC_DQBUF (capture) failed: %s", strerror(errno));
        return false;
    }

    // Query capture format (to get width/height)
    struct v4l2_format fmtget = {};
    fmtget.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(v4l2_fd, VIDIOC_G_FMT, &fmtget) < 0) {
        SDL_Log("VIDIOC_G_FMT (capture) failed: %s", strerror(errno));
        // still proceed if necessary
    } else {
        v4l2_cap_width = fmtget.fmt.pix_mp.width;
        v4l2_cap_height = fmtget.fmt.pix_mp.height;
    }

    // capbuf.index tells which cap buffer holds the decoded data
    int decoded_index = capbuf.index;
    pixeldata_out = (unsigned char*)v4l2_cap_mmap[decoded_index];
    pixeldata_len_out = v4l2_cap_mmap_len[decoded_index];
    width = v4l2_cap_width;
    height = v4l2_cap_height;


    // After consuming the capture buffer we'll re-queue it so driver can reuse it
    // set bytesused as per capbuf.m.planes[0].bytesused (driver filled)
    capbuf.m.planes[0].length = (unsigned int)pixeldata_len_out;
    capbuf.m.planes[0].bytesused = capbuf.m.planes[0].bytesused; // keep what driver returned

    if (ioctl(v4l2_fd, VIDIOC_QBUF, &capbuf) < 0) {
        SDL_Log("VIDIOC_QBUF (requeue capture) failed: %s", strerror(errno));
        // not fatal — but log
    }

    // increment indices for round-robin
    out_idx = (out_idx + 1) % v4l2_out_buf_count;
    cap_idx = (cap_idx + 1) % v4l2_cap_buf_count;

*/
    return true;
}

void _free_pixeldata(unsigned char *pixeldata, size_t pixeldata_len) {
    // we do not unmap here — mapping persists until program ends or cleanup; but
    // if you prefer to unmap now, you should not re-use the buffers. For persistent reuse keep them mapped.
    // (We leave them mapped)
}


//FIXME loader_cleanup() does not clean up properly. giving up on codec bug.
// program "works" only one time. then you must reboot the system, or else you get:
// VIDIOC_REQBUFS (output) failed: Invalid argument

/*
$ dmesg | grep -i video
[   34.695235] videodev: Linux video capture interface: v2.00
[   35.537863] bcm2835_vc_sm_cma_probe: Videocore shared memory driver
[   36.172285] bcm2835-isp bcm2835-isp: Device node output[0] registered as /dev/video13
[   36.173568] bcm2835-isp bcm2835-isp: Device node capture[0] registered as /dev/video14
[   36.185344] bcm2835-isp bcm2835-isp: Device node capture[1] registered as /dev/video15
[   36.212193] bcm2835-isp bcm2835-isp: Device node stats[2] registered as /dev/video16
[   36.376346] bcm2835-codec bcm2835-codec: Device registered as /dev/video10
[   36.378119] bcm2835-isp bcm2835-isp: Device node output[0] registered as /dev/video20
[   36.392849] bcm2835-isp bcm2835-isp: Device node capture[0] registered as /dev/video21
[   36.394398] bcm2835-isp bcm2835-isp: Device node capture[1] registered as /dev/video22
[   36.405133] bcm2835-isp bcm2835-isp: Device node stats[2] registered as /dev/video23
[   36.408183] bcm2835-codec bcm2835-codec: Device registered as /dev/video11
[   36.453353] bcm2835-codec bcm2835-codec: Device registered as /dev/video12
[   36.516345] bcm2835-codec bcm2835-codec: Device registered as /dev/video18
[   36.592471] bcm2835-codec bcm2835-codec: Device registered as /dev/video31
[  219.282444] WARNING: CPU: 0 PID: 601 at drivers/media/common/videobuf2/videobuf2-core.c:2215 __vb2_queue_cancel+0x280/0x330 [videobuf2_common]
[  219.282727] Modules linked in: vc4 hid_logitech_hidpp snd_soc_hdmi_codec drm_display_helper cec drm_dma_helper drm_kms_helper snd_soc_core snd_compress snd_pcm_dmaengine snd_pcm snd_timer snd bcm2835_codec(C) bcm2835_v4l2(C) bcm2835_isp(C) v4l2_mem2mem bcm2835_mmal_vchiq(C) raspberrypi_hwmon vc_sm_cma(C) videobuf2_vmalloc videobuf2_dma_contig videobuf2_memops videobuf2_v4l2 videodev i2c_bcm2835 videobuf2_common binfmt_misc mc raspberrypi_gpiomem joydev fixed hid_logitech_dj uio_pdrv_genirq uio zram lz4_compress fuse drm drm_panel_orientation_quirks backlight nfnetlink ip_tables x_tables ipv6
[  219.283488]  warn_slowpath_fmt from __vb2_queue_cancel+0x280/0x330 [videobuf2_common]
[  219.283702]  __vb2_queue_cancel [videobuf2_common] from vb2_core_streamoff+0x20/0xa8 [videobuf2_common]
[  219.283941]  vb2_core_streamoff [videobuf2_common] from v4l2_m2m_streamoff+0x44/0x10c [v4l2_mem2mem]
[  219.284299]  v4l2_m2m_streamoff [v4l2_mem2mem] from __video_do_ioctl+0x460/0x4ec [videodev]
[  219.285359]  __video_do_ioctl [videodev] from video_usercopy+0x260/0x5c8 [videodev]
[  219.286518]  video_usercopy [videodev] from sys_ioctl+0x2d0/0xc28
[  219.287464] videobuf2_common: driver bug: stop_streaming operation is leaving buffer 0 in active state
[  227.442634] WARNING: CPU: 0 PID: 601 at drivers/media/common/videobuf2/videobuf2-core.c:2215 __vb2_queue_cancel+0x280/0x330 [videobuf2_common]
[  227.442883] Modules linked in: vc4 hid_logitech_hidpp snd_soc_hdmi_codec drm_display_helper cec drm_dma_helper drm_kms_helper snd_soc_core snd_compress snd_pcm_dmaengine snd_pcm snd_timer snd bcm2835_codec(C) bcm2835_v4l2(C) bcm2835_isp(C) v4l2_mem2mem bcm2835_mmal_vchiq(C) raspberrypi_hwmon vc_sm_cma(C) videobuf2_vmalloc videobuf2_dma_contig videobuf2_memops videobuf2_v4l2 videodev i2c_bcm2835 videobuf2_common binfmt_misc mc raspberrypi_gpiomem joydev fixed hid_logitech_dj uio_pdrv_genirq uio zram lz4_compress fuse drm drm_panel_orientation_quirks backlight nfnetlink ip_tables x_tables ipv6
[  227.443663]  warn_slowpath_fmt from __vb2_queue_cancel+0x280/0x330 [videobuf2_common]
[  227.443851]  __vb2_queue_cancel [videobuf2_common] from vb2_core_streamoff+0x20/0xa8 [videobuf2_common]
[  227.444079]  vb2_core_streamoff [videobuf2_common] from v4l2_m2m_streamoff+0x44/0x10c [v4l2_mem2mem]
[  227.444415]  v4l2_m2m_streamoff [v4l2_mem2mem] from __video_do_ioctl+0x460/0x4ec [videodev]
[  227.445494]  __video_do_ioctl [videodev] from video_usercopy+0x260/0x5c8 [videodev]
[  227.446652]  video_usercopy [videodev] from sys_ioctl+0x2d0/0xc28
[  227.447611] videobuf2_common: driver bug: stop_streaming operation is leaving buffer 0 in active state
[  227.447639] videobuf2_common: driver bug: stop_streaming operation is leaving buffer 1 in active state
[  227.447714] videobuf2_common: driver bug: stop_streaming operation is leaving buffer 2 in active state
[  227.447740] videobuf2_common: driver bug: stop_streaming operation is leaving buffer 3 in active state
[  258.802694] bcm2835-codec bcm2835-codec: bcm2835_codec_create_component: failed to create component ril.video_decode
*/

void _loader_cleanup() {

    int buf_idx = 0;
    while(buf_idx < v4l2_out_mmap.size()) {
        struct v4l2_buffer out_buf = {0};
        struct v4l2_plane out_planes[FMT_OUT_NUM_PLANES];
        memset(out_planes, 0, sizeof(out_planes));

        out_buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        out_buf.memory = V4L2_MEMORY_MMAP;
        out_buf.length = FMT_OUT_NUM_PLANES;
        out_buf.m.planes = out_planes;
        out_buf.index = buf_idx;

        if (ioctl(v4l2_fd, VIDIOC_QUERYBUF, &out_buf) < 0) {
            SDL_Log("cleanup: VIDIOC_QUERYBUF output failed: %s", strerror(errno));
        }

        if(out_buf.flags & V4L2_BUF_FLAG_DONE) { 
            if(ioctl(v4l2_fd, VIDIOC_DQBUF, &out_buf) < 0) {
                SDL_Log("cleanup: VIDIOC_DQBUF output failed: %s", strerror(errno));
            }
            else {
                SDL_Log("cleanup: VIDIOC_DQBUF output sucecess");
            }
        }

        buf_idx++;
    }

    buf_idx = 0;
    while(buf_idx < v4l2_cap_mmap.size()) {
        struct v4l2_buffer cap_buf = {0};
        struct v4l2_plane cap_planes[FMT_CAP_NUM_PLANES];
        memset(cap_planes, 0, sizeof(cap_planes));

        cap_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        cap_buf.memory = V4L2_MEMORY_MMAP;
        cap_buf.length = FMT_CAP_NUM_PLANES;
        cap_buf.m.planes = cap_planes;
        cap_buf.index = buf_idx;

        if (ioctl(v4l2_fd, VIDIOC_QUERYBUF, &cap_buf) < 0) {
            SDL_Log("cleanup: VIDIOC_QUERYBUF capture failed: %s", strerror(errno));
        }

        if(cap_buf.flags & V4L2_BUF_FLAG_DONE) { 
            if(ioctl(v4l2_fd, VIDIOC_DQBUF, &cap_buf) < 0) {
                SDL_Log("cleanup: VIDIOC_DQBUF capture failed: %s", strerror(errno));
            }
            else {
                SDL_Log("cleanup: VIDIOC_DQBUF capture sucecess");
            }
        }

        buf_idx++;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type) < 0)
        SDL_Log("cleanup: VIDIOC_STREAMOFF output failed: %s", strerror(errno));

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type) < 0)
        SDL_Log("cleanup: VIDIOC_STREAMOFF capture failed: %s", strerror(errno));

    struct v4l2_requestbuffers req = {0};
    req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = 0;
    if (ioctl(v4l2_fd, VIDIOC_REQBUFS, &req) < 0)
        SDL_Log("cleanup: VIDIOC_REQBUFS output failed: %s", strerror(errno));

    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    req.count = 0;
    if (ioctl(v4l2_fd, VIDIOC_REQBUFS, &req) < 0)
        SDL_Log("cleanup: VIDIOC_REQBUFS capture failed: %s", strerror(errno));

    for (auto &buf : v4l2_out_mmap) {
        for (int i=0; i < FMT_OUT_NUM_PLANES; i++) {
            if (buf.start[i] != 0 && buf.start[i] != MAP_FAILED) {
                munmap(buf.start[i], buf.length[i]);
            }
        }
    }

    for (auto &buf : v4l2_cap_mmap) {
        for (int i=0; i < FMT_CAP_NUM_PLANES; i++) {
            if (buf.start[i] != 0 && buf.start[i] != MAP_FAILED) {
                munmap(buf.start[i], buf.length[i]);
            }
        }
    }

    close(v4l2_fd); v4l2_fd = -1;
}