// #define RADIOLIB_CUSTOM_ARDUINO 1
// #define RADIOLIB_TONE_UNSUPPORTED 1
// #define RADIOLIB_SOFTWARE_SERIAL_UNSUPPORTED 1

#define ARDUINO_ARCH_AVR

#ifndef HAS_WIFI
#define HAS_WIFI 1
#endif

// default I2C pins:
// SDA = 4
// SCL = 5

// Recommended pins for SerialModule:
// txd = 8
// rxd = 9

#define EXT_NOTIFY_OUT 22
#define BUTTON_PIN 17

#define LED_PIN LED_BUILTIN

// ST7735S TFT LCD
#define ST7735S 1 // there are different (sub-)versions of ST7735
#define ST7735_CS 20
#define ST7735_RS 22  // DC
#define ST7735_SDA 19 // MOSI
#define ST7735_SCK 18
#define ST7735_RESET 26
#define ST7735_MISO -1
#define ST7735_BUSY -1
#define ST7735_BL_V05 21 /* V1.1 PCB marking */
#define ST7735_SPI_HOST SPI3_HOST
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define SCREEN_ROTATE
#define TFT_HEIGHT DISPLAY_WIDTH
#define TFT_WIDTH DISPLAY_HEIGHT
#define TFT_OFFSET_X 26
#define TFT_OFFSET_Y -1
#define SCREEN_TRANSITION_FRAMERATE 3 // fps
#define DISPLAY_FORCE_SMALL_FONTS

#define BATTERY_PIN 26
// ratio of voltage divider = 3.0 (R17=200k, R18=100k)
#define ADC_MULTIPLIER 3.1 // 3.0 + a bit for being optimistic
#define BATTERY_SENSE_RESOLUTION_BITS ADC_RESOLUTION

#define USE_SX1262

#undef LORA_SCK
#undef LORA_MISO
#undef LORA_MOSI
#undef LORA_CS

#define LORA_SCK 10
#define LORA_MISO 12
#define LORA_MOSI 11
#define LORA_CS 3

#define LORA_DIO0 RADIOLIB_NC
#define LORA_RESET 15
#define LORA_DIO1 20
#define LORA_DIO2 2
#define LORA_DIO3 RADIOLIB_NC

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8
#endif