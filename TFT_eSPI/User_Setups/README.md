- **IMPORTANT:** You will need to copy the file *"esp_gaming_clock_ILI9341"* inside the TFT_eSPI library under *"Arduino\libraries\TFT_eSPI\User_Setups\"* and enable that file by editing *"User_Setup_Select.h"* in that library, commenting out the default include and creating a new include:
> `//#include <User_Setup.h>           // Default setup is root library folder`
> `#include <User_Setups/esp_gaming_clock_ILI9341.h>  // Setup file for ESP8266 configured for my ILI9341`
- Make sure you edit the LCD connection pins on the file *"esp_gaming_clock_ILI9341.h"*
