#pragma once
// Retain the native T-Deck UI feature paths; host SD/audio drivers below are inert.
#include "device_caps.h"
#undef CAP_SD
#define CAP_SD 1
#undef CAP_FILESYSTEM
#define CAP_FILESYSTEM 1
#undef CAP_LARGE_SCREEN
#define CAP_LARGE_SCREEN 0
#undef CAP_LOCK_SCREEN
#define CAP_LOCK_SCREEN 1
#undef CAP_SOUND_FILES
#define CAP_SOUND_FILES 1
#include <freertos/queue.h>
#define PIN_TFT_DC 11
#define PIN_TFT_CS 12
#define PIN_USER_BTN 0
typedef int gpio_num_t;
#define RTC_GPIO_MODE_INPUT_ONLY 0
static inline int rtc_gpio_init(int)
{
    return 0;
}
static inline int rtc_gpio_set_direction(int, int)
{
    return 0;
}
