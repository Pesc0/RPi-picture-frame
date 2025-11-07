#pragma once

#include <gpiod.h>


class GPIOLED {
public:
    GPIOLED(unsigned int gpio_pin);
    ~GPIOLED();

    void set_led(bool state);

private:
    const unsigned int gpio_line;
    bool have_led;
    struct gpiod_chip *gpio_chip;
    struct gpiod_line_settings *settings;
    struct gpiod_line_config *line_cfg;
    struct gpiod_request_config *req_cfg;
    struct gpiod_line_request *request;
};