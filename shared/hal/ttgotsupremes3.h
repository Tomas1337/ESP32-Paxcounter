// clang-format off
// upload_speed 1500000
// board esp32-s3-devkitc-1

// for pinouts see https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series/blob/master/schematic/LilyGo_T-BeamS3Supreme.pdf

#ifndef _TTGOTSUPREMES3_H
#define _TTGOTSUPREMES3_H

#include <stdint.h>

// Real time clock PCF8583
//#define RTC_INT (14)

// 6-achsis unit MPU6886
// gyroscope & acceleration & temperature sensor QMI8658
// IMU_INT (33)
// IMU_CS (34)

//#define HAS_SDCARD  1      // this board has an SD-card-reader/writer
// Pins for SD-card
//#define SDCARD_CS    (47)
//#define SDCARD_MOSI  (35)
//#define SDCARD_MISO  (37)
//#define SDCARD_SCLK  (36)

// display SSH1106 address 0x1c
#define HAS_DISPLAY 1
#define MY_DISPLAY_SDA (17)
#define MY_DISPLAY_SCL (18)
#define MY_DISPLAY_RST NOT_A_PIN
//#define MY_DISPLAY_FLIP  1 // use if display is rotated

// button and led
#define HAS_BUTTON GPIO_NUM_0       // SW3 button is on GPIO0
#define HAS_LED NOT_A_PIN
// SW2 -> CHIP_PU
// SW3 -> PWR_KEY

// GPS settings
#define HAS_GPS 1 // use on board GPS
#define GPS_SERIAL 9600, SERIAL_8N1, GPIO_NUM_9, GPIO_NUM_8 // UBlox NEO 6M
#define GPS_INT GPIO_NUM_6 // 30ns accurary timepulse generated by NEO 6M Pin #3
#define GPS_WAKEUP GPIO_NUM_7 // currently unused in code

// LORA settings
#define HAS_LORA 1 // use on board LORA
#define CFG_sx1262_radio 1 // HPD16A
// Pins for LORA chip SPI interface, reset line and interrupt lines
#define LORA_IRQ  (1)   // RADIO_DIO1
#define LORA_IO1  LMIC_UNUSED_PIN
#define LORA_IO2  LMIC_UNUSED_PIN
#define LORA_BUSY (4)   // RADIO_BUSY
#define LORA_RST  (5)   // RADIO_NRSET
#define LORA_CS   (10)  // RADIO_CD
#define LORA_MOSI (11)  // RADIO_MOSI
#define LORA_SCK  (12)  // RADIO_SCK
#define LORA_MISO (13)  // RADIO_MISO

// BME280 sensor on I2C bus
//#define HAS_BME 1 // Enable BME sensors in general
//#define HAS_BME280 SDA, SCL // SDA, SCL
//#define BME280_ADDR 0x76 // change to 0x77 depending on your wiring

// power management settings
#define I2C_SDA1 42 // Used for PMU management and PCF8563
#define I2C_SCL1 41 // Used for PMU management and PCF8563
//
#define HAS_PMU 1 // has AXP202 chip
#define XPOWERS_CHIP_AXP202 1
#define PMU_INT GPIO_NUM_40 // battery interrupt
#define PMU_CHG_CURRENT XPOWERS_AXP202_CHG_CUR_1000MA // battery charge current
// See: xpowers_AXP202_chg_curr_t
// possible values (mA):
// 0/100/125/150/175/200/300/400/500/600/700/800/900/1000
#define PMU_CHG_CUTOFF XPOWERS_AXP202_CHG_VOL_4V2 // battery charge cutoff
// See: xpowers_AXP202_chg_vol_t
// possible values (V):
// 4V1/4V1/4V2/4V35/4V4

// power lines T-Beam Supreme
// A_LDO1: QMC6310 Magnetometer + BME280
// A_LDO2: unused
// A_LDO3: LORA
// A_LDO4: GPS
// B_LDO1: SD-Card
// B_LOD2: unused


#endif