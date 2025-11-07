// MMAL from https://github.com/6by9/userland/blob/hello_mmal/host_applications/linux/apps/hello_pi/hello_mmal_jpeg/jpeg.c

#include "bcm_host.h"
#include "interface/mmal/mmal.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_connection.h"
#include "interface/mmal/mmal_queue.h"
#include "interface/vcos/vcos.h"

#include <vector>
#include <string>
#include <SDL3/SDL.h>
#include <cstddef>


#define LOADER_GL_PIXEL_FORMAT GL_RGBA

static MMAL_COMPONENT_T* decode_component = nullptr;
static MMAL_POOL_T *input_pool = nullptr, *output_pool = nullptr;
static MMAL_QUEUE_T* output_queue = nullptr;
VCOS_SEMAPHORE_T semaphore;
MMAL_STATUS_T callback_status;


// Callback for output port
static void output_callback(MMAL_PORT_T* port, MMAL_BUFFER_HEADER_T* buffer) {
    mmal_queue_put(output_queue, buffer);
    vcos_semaphore_post(&semaphore);
}

// Callback for input port
static void input_callback(MMAL_PORT_T* port, MMAL_BUFFER_HEADER_T* buffer) {
    mmal_buffer_header_release(buffer);
    vcos_semaphore_post(&semaphore);
}

// Callback for control port
static void control_callback(MMAL_PORT_T* port, MMAL_BUFFER_HEADER_T* buffer) {
    if (buffer->cmd == MMAL_EVENT_ERROR) {
        callback_status = *(MMAL_STATUS_T *)buffer->data;
        SDL_Log("control callback: err");
    }
    else {
        callback_status = MMAL_SUCCESS;
    }
    mmal_buffer_header_release(buffer);
    vcos_semaphore_post(&semaphore);
}


bool _init_img_loader() {
    bcm_host_init();
    vcos_semaphore_create(&semaphore, "imgloader", 1);

    MMAL_STATUS_T status = mmal_component_create(MMAL_COMPONENT_DEFAULT_IMAGE_DECODER, &decode_component);
    if (status != MMAL_SUCCESS || !decode_component) {
        SDL_Log("Failed to create MMAL decode component");
        return false;
    }

    // Enable control port
    status = mmal_port_enable(decode_component->control, control_callback);
    if (status != MMAL_SUCCESS) {
        SDL_Log("Failed to enable control port");
        return false;
    }

    // Configure input port
    MMAL_PORT_T* input = decode_component->input[0];
    input->format->encoding = MMAL_ENCODING_JPEG;
    input->buffer_num = input->buffer_num_recommended;
    input->buffer_size = input->buffer_size_recommended;
    status = mmal_port_format_commit(input);
    if (status != MMAL_SUCCESS) {
        SDL_Log("Failed to commit input port format");
        return false;
    }

    // Configure output port
    MMAL_PORT_T* output = decode_component->output[0];
    output->format->encoding = MMAL_ENCODING_RGBA;
    output->buffer_num = output->buffer_num_recommended;
    output->buffer_size = output->buffer_size_recommended; 
    status = mmal_port_format_commit(output);
    if (status != MMAL_SUCCESS) {
        SDL_Log("Failed to commit output port format");
        return false;
    }

    // Enable ports
    status = mmal_port_enable(input, input_callback);
    if (status != MMAL_SUCCESS) {
        SDL_Log("Failed to enable input port");
        return false;
    }
    status = mmal_port_enable(output, output_callback);
    if (status != MMAL_SUCCESS) {
        SDL_Log("Failed to enable output port");
        return false;
    }

    // Create pools
    input_pool = mmal_port_pool_create(input, input->buffer_num, input->buffer_size);
    output_pool = mmal_port_pool_create(output, output->buffer_num, output->buffer_size);
    if (!input_pool || !output_pool) {
        SDL_Log("Failed to create MMAL buffer pools");
        return false;
    }

    // Create output queue
    output_queue = mmal_queue_create();


    /* Send empty buffers to the output port of the decoder */
    MMAL_BUFFER_HEADER_T* buf;
    while ((buf = mmal_queue_get(output_pool->queue)) != NULL)
    {
        //printf("Sending buf %p\n", buffer);
        if (mmal_port_send_buffer(output, buf) != MMAL_SUCCESS) {
            SDL_Log("Failed to send output buffer");
            return false;
        }
    }

    // Enable component
    status = mmal_component_enable(decode_component);
    if (status != MMAL_SUCCESS) {
        SDL_Log("Failed to enable component");
        return false;
    }

    return true;
}


bool _load_image(unsigned char *&pixeldata_out, size_t &pixeldata_len_out, const std::vector<unsigned char> &filebuf_in, const std::string &path_in, int &width, int &height) {

    MMAL_BUFFER_HEADER_T *in_buf, *out_buf;

    MMAL_PORT_T* input = decode_component->input[0];
    MMAL_PORT_T* output = decode_component->output[0];

    // Feed JPEG chunks into input buffers
    size_t offset = 0;
    while (offset < filebuf_in.size()) {

        /* Wait for buffer headers to be available on either of the decoder ports */
        VCOS_STATUS_T vcos_status = vcos_semaphore_wait_timeout(&semaphore, 2000);
        if (vcos_status != VCOS_SUCCESS)
            SDL_Log("vcos_semaphore_wait_timeout failed - status %d\n", vcos_status);

        /* Check for errors */
        if (callback_status != MMAL_SUCCESS)
            break;

        MMAL_BUFFER_HEADER_T* in_buf = mmal_queue_get(input_pool->queue);

        size_t chunk = std::min((size_t)in_buf->alloc_size, filebuf_in.size() - offset);
        memcpy(in_buf->data, filebuf_in.data() + offset, chunk);
        in_buf->length = chunk;
        in_buf->offset = 0;
        in_buf->flags = (offset + chunk == filebuf_in.size()) ? MMAL_BUFFER_HEADER_FLAG_FRAME_END : 0;

        if (mmal_port_send_buffer(input, in_buf) != MMAL_SUCCESS) {
            mmal_buffer_header_release(in_buf);
            SDL_Log("Failed to send input buffer");
            return false;
        }

        offset += chunk;

        /* Send empty buffers to the output port of the decoder */
        MMAL_BUFFER_HEADER_T* buf;
        while ((buf = mmal_queue_get(output_pool->queue)) != NULL)
        {
            //printf("Sending buf %p\n", buffer);
            if (mmal_port_send_buffer(output, buf) != MMAL_SUCCESS) {
                SDL_Log("Failed to send output buffer");
                return false;
            }
        }
    }

    // Wait for decoded frame
    MMAL_BUFFER_HEADER_T* out_buf = mmal_queue_wait(output_queue);
    if (!out_buf) {
        SDL_Log("Failed to get decoded buffer");
        return false;
    }

    pixeldata_out = out_buf->data;

    return true;
}

void _free_pixeldata(unsigned char *pixeldata, size_t pixeldata_len) {
    mmal_buffer_header_release(pixeldata);
}

void _loader_cleanup() {}