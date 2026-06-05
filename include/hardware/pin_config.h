#pragma once

/** SH8601 AMOLED + CHSC5816 touch (T-Encoder Pro V1.0, DXQ120MYB2416A panel). */
#ifndef DXQ120MYB2416A
#define DXQ120MYB2416A
#endif

#ifndef CHSC5816_SLAVE_ADDRESS
#define CHSC5816_SLAVE_ADDRESS 0x2E
#endif

#define BUZZER_DATA 17

#define IIC_SDA 5
#define IIC_SCL 6

#define TOUCH_INT 9
#define TOUCH_RST 8

#define LCD_SDIO0 11
#define LCD_SDIO1 13
#define LCD_SDIO2 7
#define LCD_SDIO3 14
#define LCD_SCLK 12
#define LCD_CS 10
#define LCD_RST 4
#define LCD_WIDTH 390
#define LCD_HEIGHT 390
#define LCD_VCI_EN 3

#define KNOB_DATA_A 1
#define KNOB_DATA_B 2
#define KNOB_KEY 0
