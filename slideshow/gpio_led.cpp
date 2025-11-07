#include "gpio_led.h"

#include <SDL3/SDL.h>


#define GPIO_CHIP_NAME "/dev/gpiochip0"


GPIOLED::GPIOLED(unsigned int gpio_pin) : gpio_line(gpio_pin) {

    have_led = true;

    gpio_chip = gpiod_chip_open(GPIO_CHIP_NAME);
    if (!gpio_chip) {
        SDL_Log("Open GPIO chip failed");
        have_led = false;     
        return;
    }

    struct gpiod_chip_info *info = gpiod_chip_get_info(gpio_chip);
    const char *label = gpiod_chip_info_get_label(info);
    if (!label || (strstr(label, "bcm") == NULL && strstr(label, "BCM") == NULL)) {
        SDL_Log("Skipping GPIO chip: not Broadcom (found: %s)\n", label ? label : "unknown");
        gpiod_chip_close(gpio_chip);
        have_led = false;  
        return;
    }

    settings = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);

    line_cfg = gpiod_line_config_new();
    const unsigned int lines[] = { gpio_line };
    gpiod_line_config_add_line_settings(line_cfg, lines, 1, settings);

    req_cfg = gpiod_request_config_new();
    gpiod_request_config_set_consumer(req_cfg, "led-toggle");

    request = gpiod_chip_request_lines(gpio_chip, req_cfg, line_cfg);
    if (!request) {
        SDL_Log("GPIO request lines failed");
        gpiod_request_config_free(req_cfg);
        gpiod_line_config_free(line_cfg);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(gpio_chip);
        have_led = false;
        return;   
    }
}


GPIOLED::~GPIOLED() {
    if (have_led) {
        set_led(false);
        gpiod_line_request_release(request);
        gpiod_request_config_free(req_cfg);
        gpiod_line_config_free(line_cfg);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(gpio_chip);
    }
}


void GPIOLED::set_led(bool state) {
    if (have_led) {
        gpiod_line_request_set_value(request, gpio_line, state ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);    
    }
}