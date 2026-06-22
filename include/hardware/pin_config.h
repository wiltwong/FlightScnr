#pragma once

#define GFX_DEV_DEVICE ESP32_S3_RGB
#define GFX_BL 6

#ifndef PCF8574_SLAVE_ADDRESS
#define PCF8574_SLAVE_ADDRESS 0x21
#endif

#ifndef CST8XX_SLAVE_ADDRESS
#define CST8XX_SLAVE_ADDRESS 0x15
#endif

//#define BUZZER_DATA 17

#define IIC_SDA 38
#define IIC_SCL 39

#define KNOB_DATA_A 42
#define KNOB_DATA_B 4
#define KNOB_KEY 5
