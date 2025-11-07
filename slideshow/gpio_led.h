#pragma once

#include <gpiod.h>

#define GPIO_CHIP_NAME "/dev/gpiochip0"
#define GPIO_LINE 23  // GPIO23


class GPIOLED {
public:
    GPIOLED();
    ~GPIOLED();

    void set_led(bool state);

private:
    bool have_led;
    struct gpiod_chip *gpio_chip;
    struct gpiod_line_settings *settings;
    struct gpiod_line_config *line_cfg;
    struct gpiod_request_config *req_cfg;
    struct gpiod_line_request *request;
};