// TFT_eSPI pin/driver setup for ESP32-C3 SuperMini + ST7789 320×240.
// Loaded via platformio.ini (-include); do not edit the library User_Setup.h.

#define USER_SETUP_ID 7789

#define ST7789_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Swap to TFT_RGB if red/blue are inverted after the inversion fix below.
#define TFT_RGB_ORDER TFT_BGR

// ST7789_Init.h always sends INVON; many 320×240 modules need it cancelled.
#define TFT_INVERSION_OFF

#define TFT_MOSI 6
#define TFT_SCLK 4
#define TFT_CS   7
#define TFT_DC   3
#define TFT_RST  2
#define TFT_MISO -1
// BL is driven by firmware PWM on GPIO5 — do not define TFT_BL.

#define LOAD_GLCD
#define LOAD_FONT2

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000
