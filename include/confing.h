#pragma once

// -------- Pin Configuration ----------
#define IR_RECV_PIN     32
#define IR_SEND_PIN     23
#define BUTTON_LEARN    17
#define BUTTON_BLAST    5
#define LED_RED         15
#define LED_GREEN       19

// LCD
#define LCD_I2C_ADDR    0x27
#define LCD_COLS        16
#define LCD_ROWS        2

// EEPROM / storage
#define EEPROM_SIZE     4096
#define RAW_BUFFER_MAX  200
#define MAX_SIGNALS     10

// IR defaults
#define IR_DEFAULT_FREQ_HZ 38000
