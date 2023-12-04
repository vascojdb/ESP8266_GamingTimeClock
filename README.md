# ESP8266 Gaming time clock
**Check how much time you have played your consoles**

## What is this?
A gaming time clock that counts the time you play on your consoles, giving you statistics for your current play session, weekly, monthly and total play time.
I use this to count the time I play on my Playstation 5 and on my Nintendo Switch. Because this device connects directly to my Denon AVR, every time it turns itself on on any of the console inputs it automatically starts counting the time and it stops counting the time when I turn it off or change input.

## Why did I create this?
Apart from having all these components lying around, I usually play games in the evening, but because I don't have a clock near the TV I end up getting distracted and play till 2 or 3am. I could buy a clock but then where would be the fun in that?

## What do you need?
### Hardware
- An ESP8266 *(NodeMCU for example)*
- A TFT LCD *(I have used a 320x240 ILI9341)*
- Denon AVR with network connection *(tested on AVC-X6700W)*
- Misc stuff *(wires, USB cable and a PC)*

### Software and libraries
- Arduino IDE
- ArduinoJson library *(by Benoit Blanchon)*
- ESPAsyncTCP *(by dvarrel)*
- NTP *(by Stefan Staub)*
- TFT_eSPI *(by Bodmer)*

## Some tech details
- **IMPORTANT:** You will need to copy the file *"esp_gaming_clock_ILI9341"* inside the TFT_eSPI library under *"Arduino\libraries\TFT_eSPI\User_Setups\"* and enable that file by editing *"User_Setup_Select.h"* in that library, commenting out the default include and creating a new include:
> `//#include <User_Setup.h>           // Default setup is root library folder`
> `#include <User_Setups/esp_gaming_clock_ILI9341.h>  // Setup file for ESP8266 configured for my ILI9341`
- Make sure you edit the LCD connection pins on the file *"esp_gaming_clock_ILI9341.h"*
- This code uses some files from the library Denon-AVR-control-ESP32 from janphoffmann (https://github.com/janphoffmann/Denon-AVR-control-ESP32). The library was slightly modified to be able to run on an ESP8266
- You can touch the screen to ask for the history for each console or for a sum of all consoles, you can also reset the stored data or show general info (WiFi, IP, etc)
- The history is saved on a JSON file on LittleFS filesystem on the flash
- The code contains the ArduinoOTA and a web browser OTA page for firmware upgrades. The data is kept in between updates *(unless you make some changes to the JSON structure, and then a reset may be needed)*
- Debug is printed on the serial port (9600bps)

## Media
Will upload soon

## Future development
I'm currently working on a nice 3D printed enclosure for the project. Will publish it here once it is done.
