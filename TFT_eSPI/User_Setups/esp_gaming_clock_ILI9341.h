#define USER_SETUP_ID 1

#define ILI9341_DRIVER  // Use the ILI9341 screen module
//#define TFT_RGB_ORDER TFT_RGB  // Colour order Red-Green-Blue
//#define TFT_RGB_ORDER TFT_BGR  // Colour order Blue-Green-Red

#define TFT_CS              PIN_D8  // Chip select control
#define TFT_DC              PIN_D3  // Data Command control pin
#define TFT_RST             PIN_D0  // Reset pin

#define TFT_BL              PIN_D1  // LED back-light control pin
#define TFT_BACKLIGHT_ON    HIGH    // Level to turn ON back-light (HIGH or LOW)

#define TOUCH_CS            PIN_D2  // Chip select pin (T_CS) of touch screen
#define TOUCH_IRQ           PIN_D4  // Touch IRQ

//#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:.
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
//#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts

#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
