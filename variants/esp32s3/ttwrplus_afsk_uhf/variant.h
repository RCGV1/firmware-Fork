#pragma once

// ============================================================
// LilyGo T-TWR Plus REV 2.1 — UHF Band (400–480 MHz)
// AFSK1200 / AX.25 radio transport variant
//
// Pin definitions from LilyGo T-TWR Library (REV 2.1):
//   https://github.com/Xinyuan-LilyGO/T-TWR
// ============================================================

// ---- Radio backend selection ----
#define USE_SA868_AFSK 1
#define MESHTASTIC_EXCLUDE_LORA 1

// ---- Default operating frequency ----
#define SA868_DEFAULT_FREQUENCY_MHZ 446.500f

// ---- SA868 radio module (Serial1) ----
// ESP32 TX → SA868 RX: GPIO 39
// SA868 TX → ESP32 RX: GPIO 48
#define SA868_UART_TX_PIN 39
#define SA868_UART_RX_PIN 48

// PTT (Push To Talk) — active LOW: LOW = TX, HIGH = RX
#define SA868_PTT_PIN 41

// Power enable (PD) — must be driven HIGH to power the SA868 module
#define SA868_POWER_PIN 40

// Squelch / carrier-detect (SQL/BUSY) — HIGH when signal detected
#define SA868_BUSY_PIN 2

// ---- AFSK Audio IOs ----
// T-TWR REV 2.1: GPIO 18 is the audio output to SA868 (ESP2SA868_MIC)
// We use LEDC (not sigma-delta) to generate tones - like LilyGo example does
#define AFSK_TX_PIN 18 // LEDC output to SA868 (same as ESP2SA868_MIC)
#define AFSK_RX_PIN 1  // ADC input from SA868 (SA8682ESP_AUDIO)

// ---- Audio Routing Control (REQUIRED for REV 2.1) ----
#define MIC_CTRL_PIN 17

// ---- Display (SH1106 OLED on REV 2.1) ----
#define HAS_SCREEN 1
#define USE_SH1106 1
#define I2C_SDA 8
#define I2C_SCL 9
#define OLED_RESET -1

// ---- GPS ----
#define HAS_GPS 1
#define GPS_RX_PIN 5
#define GPS_TX_PIN 6

// ---- Rotary Encoder (REV 2.1) ----
#define ENCODER_A_PIN 47
#define ENCODER_B_PIN 46
#define ENCODER_OK_PIN 21 // Center Button (KNOB PRESS)

// ---- Buttons ----
#define BUTTON_PIN 0     // User/Boot button (Down)
#define BUTTON_PTT_PIN 3 // Physical PTT button on side
#define BUTTON_NEED_PULLUP 1

// ---- Battery ADC ----
#define BATTERY_PIN 4
#define ADC_CHANNEL ADC1_GPIO4_CHANNEL
#define ADC_MULTIPLIER 4.9

// ---- Exclude features not present ----
#ifndef MESHTASTIC_EXCLUDE_LORA
#define MESHTASTIC_EXCLUDE_LORA 1
#endif
