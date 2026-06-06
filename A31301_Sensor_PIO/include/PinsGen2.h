/* 
Pin definition for Gen2 board
#define [pin name] [GPIO number] //[package pin number]
Refer to ESP32-S3 datasheet for detailed pin layout information:
https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
*/

// NeoPixel LED and status LED
#define NPX_PIN          1      // Pin 39
#define RED_LED_PIN      42     // Pin 35
#define BLUE_LED_PIN     48     // Pin 25 - Gen2 only
#define MAG_LED_PIN      3      // Pin 15 - Gen2 only

// I2C pins
#define SCL_PIN          10     // Pin 18
#define SDA_PIN          11     // Pin 19

// Power
#define POWER_GATE       21     // Pin 
#define POWER_SW         47     // Pin 24

// Servo control
#define SERVO_PIN1       15     // Pin 8
#define SERVO_PIN2       2      // Pin 38

// OLED reset
#define OLED_RESET_PIN   9      // Pin 17

// Joystick buttons
#define DOWN_PIN         38     // Pin 31
#define BACK_PIN         39     // Pin 32    
#define UP_PIN           40     // Pin 33
#define ENTER_PIN        41     // Pin 34

// LoRa SPI interface
#define LORA_SCK_PIN     5      // Pin 5  - Gen2 only
#define LORA_MOSI_PIN    6      // Pin 6  - Gen2 only
#define LORA_MISO_PIN    7      // Pin 7  - Gen2 only
#define LORA_CS_PIN      4      // Pin 4  - Gen2 only
#define LORA_RST_PIN     16     // Pin 9  - Gen2 only
#define LORA_IRQ_PIN     17     // Pin 10 - Gen2 only
#define LORA_BUSY_PIN    18     // Pin 11 - Gen2 only

// Battery voltage
#define BATT_SENSE_PIN   12     // Pin 38 - Placeholder to suppress build errors on Gen2 boards where battery sensing is not used.

