// ==============================================================
//                  ESP8266 Gaming time clock
//       Check how much time you have played your consoles
//  
// This project aims to create a clock running on an ESP8266
// that gathers time via an NTP server and displays it on an LCD.
//
// Additionally it connects to a Denon AVR on the same network
// and monitors when its power changes and when specific inputs
// are activated. Once this is done it starts counting the time
// a specific output is active (in this example one input is
// connected to a Playstation 5 and another to a Nintendo Switch)
//
// With this a daily, weekly, monthly and total play time history
// is created and displayed on the screen at the end of the play
// session.
//
// The user can touch the screen to ask for the history, info or
// to reset the saved history.
// The history is saved on a JSON file on a LittleFS on the flash
//
// Created by vascojdb on December 4th, 2023
//
// This project uses a modified version of janphoffmann library:
// https://github.com/janphoffmann/Denon-AVR-control-ESP32
//
// The LCD settings are stored in LCD_Setup.h. Make sure to
// include that file before the TFT_eSPI library
//
// Debug is printed on the serial port at 9600 baud rate
// ==============================================================
#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <NTP.h>
//#include "LCD_Setup.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include "DenonCommands.h"
#include "DenonAVR.h"

#define SOFTWARE_VERSION        "v1.0"

// ==============================================================
// START OF USER PARAMETERS
// ==============================================================
// Wi-Fi and network settings:
#define WIFI_SSID               "yourwifi"
#define WIFI_PASSWORD           "yourpassword"
#define NETWORK_HOSTNAME        "esp-gaming-clock"
// Waiting time for the WiFi connection before restarting (seconds)
#define WIFI_RETRY_TIMEOUT      10

// Screen settings:
// Note: The colors are all defined in the file TFT_eSPI.h from TFT_eSPI library
// This is the default color of the clock
#define CLOCK_DEFAULT_COLOR     TFT_WHITE
// If we are playing, the clock will change to this color from CLOCK_LATE_HOUR
#define CLOCK_LATE_COLOR        TFT_ORANGE      
#define CLOCK_LATE_HOUR         23
// If we are playing, the clock will change to this color from CLOCK_VERYLATE_HOUR
#define CLOCK_VERYLATE_COLOR    TFT_RED
#define CLOCK_VERYLATE_HOUR     0
// The hour where the current day session will end:
// Note: This is not 0 (00:00) because most of the time we end playing after midnight but always before 6am:
#define CLOCK_ENDSESSION_HOUR   6
// When does the new week start (so statistics are stored): Sunday=0, Monday=1, etc
#define CLOCK_STARTWEEK_DAY     1
// Set here the brightness while on idle and when active (0-100%)
#define SCREEN_IDLE_BRIGHTNESS  5
#define SCREEN_ON_BRIGHTNESS    100
// Adjust here the debounce settings for the touch screen (in ms)
#define TFT_TOUCH_DEBOUNCE_TIME 100

// Denon AVR settings:
// Set your Denon AVR IP (xxx,xxx,xxx,xxx)
#define AVR_IP                  192,168,0,22
// Set the proper inputs from the AVR protocol (see DenonCommands.h)
#define AVR_INPUT_PS5           BLURAY_DISC
#define AVR_INPUT_SWITCH        GAME
// Set the user friendly names (to be displayed on the LCD)
#define AVR_INPUT_PS5_NAME      "Playstation 5"
#define AVR_INPUT_SWITCH_NAME   "Nintendo Switch"
// Set the background colors for each console (to be displayed on the LCD)
#define AVR_INPUT_PS5_COLOR     TFT_NAVY
#define AVR_INPUT_SWITCH_COLOR  TFT_MAROON

// NTP timezones and DST rules:
// NTP timezones and rules should be set in ntp_init()

// ==============================================================
// END OF USER PARAMETERS
// Don't modify beyond this point. It might break the code.
// ==============================================================

// Filesystem defines (for JSON data file):
// Warning: Dont touch this or you may break the data file
#define TIME_PS5_TODAY          "ps_t"
#define TIME_PS5_WEEK           "ps_w"
#define TIME_PS5_MONTH          "ps_m"
#define TIME_PS5_YEAR           "ps_y"
#define TIME_PS5_SUM            "ps_s"
#define TIME_SWITCH_TODAY       "sw_t"
#define TIME_SWITCH_WEEK        "sw_w"
#define TIME_SWITCH_MONTH       "sw_m"
#define TIME_SWITCH_YEAR        "sw_y"
#define TIME_SWITCH_SUM         "sw_s"
#define JSON_BUFFER_SIZE        200
#define FS_DATA_FILE            "/data.json"

// Variables:
const char code_compile_date[] = __DATE__ " " __TIME__;
const char code_version[] = SOFTWARE_VERSION;
const char host_name[] = NETWORK_HOSTNAME;
const char ssid[] = WIFI_SSID;
const char password[] = WIFI_PASSWORD;

const char avr_input_ps5_n[] = AVR_INPUT_PS5_NAME;
const char avr_input_switch_n[] = AVR_INPUT_SWITCH_NAME;

uint32_t everySecond_tick = 0;
uint32_t everyMinute_tick = 0;
uint32_t ntpUpdate_tick = 0;

uint32_t time_ps5_today_mins = 0;
uint32_t time_ps5_week_mins = 0;
uint32_t time_ps5_month_mins = 0;
uint32_t time_ps5_year_mins = 0;
uint32_t time_ps5_sum_mins = 0;
uint32_t time_switch_today_mins = 0;
uint32_t time_switch_week_mins = 0;
uint32_t time_switch_month_mins = 0;
uint32_t time_switch_year_mins = 0;
uint32_t time_switch_sum_mins = 0;

uint8_t  t_hour = 0, t_minute = 0, t_hour_last = 0, t_minute_last = 0;
uint8_t  t_day = 0, t_month = 0, t_weekday = 0;
uint16_t t_year = 0;

// Screen state machine:
#define SM_SCREEN_OFF           0
#define SM_SCREEN_IDLE          1
#define SM_SCREEN_INFO          2
#define SM_SCREEN_STATS_ALL     3
#define SM_SCREEN_PLAY_PS5      10
#define SM_SCREEN_STATS_PS5     11
#define SM_SCREEN_PLAY_SWITCH   20
#define SM_SCREEN_STATS_SWITCH  21
uint8_t sm_screen = SM_SCREEN_IDLE;
uint8_t sm_nextScreen = SM_SCREEN_IDLE;
int8_t  sm_timeToNextScreen = 0;
bool    sm_screen_forceRefresh = false;
bool    tft_touchIRQ = false;

// ==============================================================
// CONSTRUCTORS
// ==============================================================
WiFiUDP           wifiUdp;
NTP               ntp(wifiUdp);
TFT_eSPI          tft = TFT_eSPI();

ESP8266WebServer        httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

IPAddress         DENON_IP(AVR_IP);
DENON_AVR         DenonAvr;
DenonProperties   *DenonAvr_Power = new DenonProperties(MAIN_ZONE);
DenonProperties   *DenonAvr_Input = new DenonProperties(INPUT_SOURCE);

// ==============================================================
// FUNCTIONS
// ==============================================================
// Builds a string with the time:
String clock_getTimeString() {
  String t_str = "";
  if (t_hour < 10) t_str += '0';
  t_str += t_hour;
  t_str += ':';
  if (t_minute < 10) t_str += '0';
  t_str += t_minute;
  return t_str;
}
// Builds a string with the date:
String clock_getDateString() {
  String d_str = "";
  if (t_day < 10) d_str += '0';
  d_str += t_day;
  d_str += '.';
  if (t_month < 10) d_str += '0';
  d_str += t_month;
  d_str += '.';
  d_str += t_year;
  return d_str;
}
// Converts a duration in minutes to the (h)(h)h:mm format
String minutes_to_str(uint32_t minutes) {
  uint32_t hours = minutes / 60;
  minutes %= 60;
  char formattedTime[7]; // "hhh:mm\0"
  if (hours < 10)
    snprintf(formattedTime, sizeof(formattedTime), "%01lu:%02lu", hours, minutes);
  else if (hours < 100)
    snprintf(formattedTime, sizeof(formattedTime), "%02lu:%02lu", hours, minutes);
  else
    snprintf(formattedTime, sizeof(formattedTime), "%03lu:%02lu", hours, minutes);
  return String(formattedTime);
}
// Converts the AVR IP format into a user readable string:
String avr_ip_to_str(uint8_t octet1, uint8_t octet2, uint8_t octet3, uint8_t octet4) {
  return String(String(octet1) + "." + String(octet2) + "." + String(octet3) + "." + String(octet4));
}
// Restarts the ESP:
void restart() {
  Serial.println("SYS: Restarting");
  delay(500);
  ESP.restart();
}

// --------------------------------------------------------------
// DEBUG/LOGGING RELATED FUNCTIONS
// --------------------------------------------------------------
// Initializes serial port:
void serial_init() {
  Serial.begin(9600);
  Serial.println("");
}
// Initializes the wifi:
void wifi_init() {
  uint8_t timeout = 0;
  Serial.println("WIFI: Connecting...");
  tft_message("Wi-Fi", "Connecting...");
  WiFi.hostname(NETWORK_HOSTNAME);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    if (timeout >= WIFI_RETRY_TIMEOUT) {
      Serial.println("WIFI: Connection timeout");
      tft_message("Wi-Fi", "Connection timeout");
      restart();
    }
    else timeout++;
  }
  Serial.println("WIFI: Connected!");
  tft_message("Wi-Fi", "Connected!");
}

// --------------------------------------------------------------
// FILESYSTEM RELATED FUNCTIONS
// --------------------------------------------------------------
// Initializes the filesystem:
void fs_init() {
  Serial.println("FS: Initializing...");
  tft_message("FS", "Initializing...");
  while (!LittleFS.begin()) {
    Serial.println("FS: LittleFS mount failed! Attempting format");
    if (!LittleFS.format())
      Serial.println("FS: Format failed");
    else
      Serial.println("FS: Format completed");
    restart();
  }
  Serial.println("FS: Ready");
  tft_message("FS", "Ready");
}
// Creates a new data file
bool fs_create_data_file() {
  Serial.println("FS: Creating a new data file");
  File file = LittleFS.open(FS_DATA_FILE, "w");
  if (!file) {
    Serial.println("FS: Failed to open file for writing");
  }
  else if (file.print("{}")) {
    Serial.println("FS: Data file created");
    file.close();
    return true;
  }
  else {
    Serial.println("FS: Error creating data file");
    file.close();
  }
  return false;
}
// Saves all the data to the filesystem:
void fs_save_data_file() {
  File jsonFile = LittleFS.open(FS_DATA_FILE, "r+");
  if (!jsonFile) {
    Serial.println("FS: Data file does not exist");
    if (!fs_create_data_file()) return;
    else jsonFile = LittleFS.open(FS_DATA_FILE, "r+");
    if (!jsonFile) return;
  }
  // Read the file content
  String jsonContent = jsonFile.readString();
  // Parse JSON content
  StaticJsonDocument<JSON_BUFFER_SIZE> data;
  DeserializationError error = deserializeJson(data, jsonContent);
  if (error) {
    Serial.print("FS: Failed to parse data: ");
    Serial.println(error.c_str());
    jsonFile.close();
    return;
  }
  // Update the data
  data[TIME_PS5_TODAY] = time_ps5_today_mins;
  data[TIME_PS5_WEEK] = time_ps5_week_mins;
  data[TIME_PS5_MONTH] = time_ps5_month_mins;
  data[TIME_PS5_YEAR] = time_ps5_year_mins;
  data[TIME_PS5_SUM] = time_ps5_sum_mins;
  data[TIME_SWITCH_TODAY] = time_switch_today_mins;
  data[TIME_SWITCH_WEEK] = time_switch_week_mins;
  data[TIME_SWITCH_MONTH] = time_switch_month_mins;
  data[TIME_SWITCH_YEAR] = time_switch_year_mins;
  data[TIME_SWITCH_SUM] = time_switch_sum_mins;
  // Seek to the beginning of the file to overwrite its content
  jsonFile.seek(0);
  jsonFile.truncate(jsonFile.position());
  // Serialize the updated JSON document and write it to the file
  if (serializeJson(data, jsonFile) == 0)
      Serial.println("FS: Failed to write updated JSON to data file");
  else
      Serial.println("FS: Data file updated");
  jsonFile.close();
}
// Loads all the data to the filesystem:
int32_t fs_load_data_file() {
  int32_t duration = 0;
  File jsonFile = LittleFS.open(FS_DATA_FILE, "r");
  if (!jsonFile) {
    Serial.println("FS: JSON file does not exist");
    if (!fs_create_data_file()) return duration;
    // Try again
    else jsonFile = LittleFS.open(FS_DATA_FILE, "r");
    if (!jsonFile) return duration;
  }
  // Read the file content
  String jsonContent = jsonFile.readString();
  jsonFile.close();
  // Parse JSON content
  StaticJsonDocument<JSON_BUFFER_SIZE> data;
  DeserializationError error = deserializeJson(data, jsonContent);
  if (error) {
    Serial.print("FS: Failed to parse JSON: ");
    Serial.println(error.c_str());
    return duration;
  }
  // Make sure the key exists before reading it
  if (data.containsKey(TIME_PS5_TODAY))     time_ps5_today_mins     = (int32_t)data[TIME_PS5_TODAY];
  if (data.containsKey(TIME_PS5_WEEK))      time_ps5_week_mins      = (int32_t)data[TIME_PS5_WEEK];
  if (data.containsKey(TIME_PS5_MONTH))     time_ps5_month_mins     = (int32_t)data[TIME_PS5_MONTH];
  if (data.containsKey(TIME_PS5_YEAR))      time_ps5_year_mins      = (int32_t)data[TIME_PS5_YEAR];
  if (data.containsKey(TIME_PS5_SUM))       time_ps5_sum_mins       = (int32_t)data[TIME_PS5_SUM];
  if (data.containsKey(TIME_SWITCH_TODAY))  time_switch_today_mins  = (int32_t)data[TIME_SWITCH_TODAY];
  if (data.containsKey(TIME_SWITCH_WEEK))   time_switch_week_mins   = (int32_t)data[TIME_SWITCH_WEEK];
  if (data.containsKey(TIME_SWITCH_MONTH))  time_switch_month_mins  = (int32_t)data[TIME_SWITCH_MONTH];
  if (data.containsKey(TIME_SWITCH_YEAR))   time_switch_year_mins   = (int32_t)data[TIME_SWITCH_YEAR];
  if (data.containsKey(TIME_SWITCH_SUM))    time_switch_sum_mins    = (int32_t)data[TIME_SWITCH_SUM];
  return duration;
}
// Resets the data file:
void fs_reset_data_file() {
  time_ps5_today_mins = 0;
  time_ps5_week_mins = 0;
  time_ps5_month_mins = 0;
  time_ps5_year_mins = 0;
  time_ps5_sum_mins = 0;
  time_switch_today_mins = 0;
  time_switch_week_mins = 0;
  time_switch_month_mins = 0;
  time_switch_year_mins = 0;
  time_switch_sum_mins = 0;
  fs_save_data_file();
}

// --------------------------------------------------------------
// TIME/DURATION RELATED FUNCTIONS
// --------------------------------------------------------------
// Initiates the ticker for handling time/duration updates:
void time_init() {
  everySecond_tick = millis() + 1000;
  everyMinute_tick = millis() + 60000;
  fs_load_data_file();
}
// Handles time duration updates in the main loop:
void time_handle() {
  bool doSave = false;
  if (everyMinute_tick < millis()) {
    everyMinute_tick = millis() + 60000;
    // Every minute, increase 1 minute of play time if we are playing:
    switch(sm_screen) {
      case SM_SCREEN_PLAY_PS5:
        time_ps5_today_mins++;
        time_ps5_week_mins++;
        time_ps5_month_mins++;
        time_ps5_year_mins++;
        time_ps5_sum_mins++;
        doSave = true;
        break;
      case SM_SCREEN_PLAY_SWITCH:
        time_switch_today_mins++;
        time_switch_week_mins++;
        time_switch_month_mins++;
        time_switch_year_mins++;
        time_switch_sum_mins++;
        doSave = true;
        break;
      default:
        break;
    }
  }
  // The current session ends at CLOCK_ENDSESSION_HOUR
  if (t_hour == CLOCK_ENDSESSION_HOUR && t_minute == 0) {
    time_ps5_today_mins = 0;
    time_switch_today_mins = 0;
    doSave = true;
    // If additionally to that its CLOCK_STARTWEEK_DAY, then we reset the week counter
    if (t_weekday == CLOCK_STARTWEEK_DAY) {
      time_ps5_week_mins = 0;
      time_switch_week_mins = 0;
    }
    // If additionally its the 1st day of a month, we reset the month counter
    if (t_day == 1) {
      time_ps5_month_mins = 0;
      time_switch_month_mins = 0;
      // If additionally its January, we reset the year counter
      if (t_month == 0) {
        time_ps5_year_mins = 0;
        time_switch_year_mins = 0;
      }
    }
  }
  if (doSave) fs_save_data_file();
}

// --------------------------------------------------------------
// SCREEN RELATED FUNCTIONS
// --------------------------------------------------------------
// Initiates the backlight:
void tft_backlight_init() {
  pinMode(TFT_BL, OUTPUT);
  analogWrite(TFT_BL, 0);
}
// Sets the backlight to a percentage (0-100):
void tft_backlight_set(uint8_t percent) {
  analogWrite(TFT_BL, map(percent, 0, 100, 0, 255));
}
// Clears the screen:
void tft_clearScreen() {
  tft.fillScreen(TFT_BLACK);
}
// Displays a welcome message with the version and compilation date:
void tft_welcomeMessage() {
  tft_clearScreen();
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Welcome", 160, 100, 4);
  tft.setTextSize(1);
  tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
  tft.drawString(code_version, 160, 160, 4);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(code_compile_date, 160, 190, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
}
// Displays a 2 line message:
void tft_message(String message1, String message2) {
  tft_clearScreen();
  tft_backlight_set(SCREEN_ON_BRIGHTNESS);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  if (message2.length() == 0)
    tft.drawString(message1, 160, 120, 4);
  else {
    tft.drawString(message1, 160, 95, 4);
    tft.drawFastHLine(60, 111, 200, TFT_WHITE);
    tft.drawFastHLine(60, 112, 200, TFT_WHITE);
    tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    tft.drawString(message2, 160, 140, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }
}
// Displays a single line message:
void tft_message(String message) {
  tft_message(message, "");
}
// Handles the screen touches (RAM-stored callback):
void ICACHE_RAM_ATTR tft_touch_handleIRQ() {
  tft_touchIRQ = true;
}
// Initializes the touch screen and the IRQ:
void tft_touch_init() {
  // Set touch:
  pinMode(TOUCH_IRQ, INPUT);
  attachInterrupt(digitalPinToInterrupt(TOUCH_IRQ), tft_touch_handleIRQ, FALLING);
}
// Handles the touch screen:
void tft_touch_handle() {
  // Handle touch if someone pressed the screen:
  if (tft_touchIRQ) {
    // Debounce
    delay(TFT_TOUCH_DEBOUNCE_TIME);
    tft_touchIRQ = false;
    // Make sure the screen is still pressed:
    if (!digitalRead(TOUCH_IRQ)) {
      // Only operates the screen if its on Off or Idle
      if (sm_screen == SM_SCREEN_OFF || sm_screen == SM_SCREEN_IDLE) {
        uint32_t pressedTime = 0;
        uint8_t option = 0;
        tft_backlight_set(SCREEN_ON_BRIGHTNESS);
        while (!digitalRead(TOUCH_IRQ)) {
          delay(1);
          pressedTime++;
          // Choose an option depending on the time the scrteen is pressed:
          if (pressedTime == 1000) {
            tft_message("History", "Release to show PS5");
            option = 1;
          }
          else if (pressedTime == 2000) {
            tft_message("History", "Release to show Switch");
            option = 2;
          }
          else if (pressedTime == 3000) {
            tft_message("History", "Release to show all");
            option = 3;
          }
          else if (pressedTime == 4000) {
            tft_message("Info", "Release to show info");
            option = 4;
          }
          else if (pressedTime == 6000) {
            tft_message("Restart", "Release to restart");
            option = 5;
          }
          else if (pressedTime == 8000) {
            tft_message("Reset", "Release to reset");
            option = 6;
          }
          else if (pressedTime == 10000) {
            tft_message("Exit", "Release to exit");
            option = 99;
          }
        }
        //Serial.print("TFT: Touch time: ");
        //Serial.println(pressedTime);
        // Execute the action set by the option:
        switch (option) {
          case 1:
            sm_screen = SM_SCREEN_STATS_PS5;
            Serial.println("TFT: Touch: PS5 stats");
            break;
          case 2:
            sm_screen = SM_SCREEN_STATS_SWITCH;
            Serial.println("TFT: Touch: Switch stats");
            break;
          case 3:
            sm_screen = SM_SCREEN_STATS_ALL;
            Serial.println("TFT: Touch: All stats");
            break;
          case 4:
            sm_screen = SM_SCREEN_INFO;
            Serial.println("TFT: Touch: Info");
            break;
          case 5:
            Serial.println("TFT: Touch: Restart");
            tft_message("Restart", "Restarting...");
            delay(2000);  // Hold the message for a bit longer
            restart();
          case 6:
            Serial.println("TFT: Touch: Reset data");
            fs_reset_data_file();
            tft_message("Reset", "Reset done!");
            Serial.println("TFT: Touch: Reset done");
            delay(2000);  // Hold the message for a bit longer
            break;
          case 0:
            delay(2000);  // Keep the backlight on for a bit
          default:
            sm_screen = SM_SCREEN_IDLE;
            Serial.println("TFT: Touch: Return to idle");
            break;
        }
        sm_screen_forceRefresh = true;
      }
    }
  }
}
// Initializes the TFT screen:
void tft_init() {
  Serial.println("TFT: Starting...");
  tft.init();
  tft.setRotation(1);
  tft_backlight_init();
  tft_backlight_set(100);
  tft_clearScreen();
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  Serial.println("TFT: Started");
  tft_welcomeMessage();
  delay(3000);
}
// Keep track of a timeout time and waits to change to the next screen:
void tft_handle_nextScreen() {
  if (sm_timeToNextScreen > 0)
    sm_timeToNextScreen--;
  else if (sm_timeToNextScreen == 0) {
    sm_timeToNextScreen = -1;
    sm_screen = sm_nextScreen;
    sm_screen_forceRefresh = true;
  }
}
// Displays the screen: Idle:
void tft_screen_idle() {
  tft_backlight_set(SCREEN_IDLE_BRIGHTNESS);
  tft.setTextColor(CLOCK_DEFAULT_COLOR, TFT_BLACK);
  tft.drawString(clock_getTimeString(), 160, 80, 8);
  tft.drawString(clock_getDateString(), 160, 180, 6);
}
// Displays the screen: Stats (all):
void tft_screen_StatsAll() {
  tft_backlight_set(SCREEN_ON_BRIGHTNESS);
  tft.fillRoundRect(10, 5, 300, 230, 20, TFT_DARKGREEN);
  tft.drawRoundRect(10, 5, 300, 230, 20, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Gaming time", 160, 30, 4);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Week:", 25, 55, 4);
  tft.drawString("Month:", 25, 113, 4);
  tft.drawString("Total:", 25, 171, 4);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(minutes_to_str(time_ps5_week_mins + time_switch_week_mins), 295, 55, 7);
  tft.drawString(minutes_to_str(time_ps5_month_mins + time_switch_month_mins), 295, 113, 7);
  tft.drawString(minutes_to_str(time_ps5_sum_mins + time_switch_sum_mins), 295, 171, 7);
  tft.setTextDatum(MC_DATUM);
}
// Displays the screen: Stats (PS5):
void tft_screen_StatsPS5() {
  tft_backlight_set(SCREEN_ON_BRIGHTNESS);
  tft.fillRoundRect(10, 5, 300, 230, 20, AVR_INPUT_PS5_COLOR);
  tft.drawRoundRect(10, 5, 300, 230, 20, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(avr_input_ps5_n, 160, 30, 4);
  tft.setTextColor(TFT_WHITE, AVR_INPUT_PS5_COLOR);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Today:", 25, 55, 4);
  tft.drawString("Week:", 25, 113, 4);
  tft.drawString("Month:", 25, 171, 4);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(minutes_to_str(time_ps5_today_mins), 295, 55, 7);
  tft.drawString(minutes_to_str(time_ps5_week_mins), 295, 113, 7);
  tft.drawString(minutes_to_str(time_ps5_month_mins), 295, 171, 7);
  tft.setTextDatum(MC_DATUM);
}
// Displays the screen: Stats (Nintendo Switch):
void tft_screen_StatsSwitch() {
  tft_backlight_set(SCREEN_ON_BRIGHTNESS);
  tft.fillRoundRect(10, 5, 300, 230, 20, AVR_INPUT_SWITCH_COLOR);
  tft.drawRoundRect(10, 5, 300, 230, 20, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(avr_input_switch_n, 160, 30, 4);
  tft.setTextColor(TFT_WHITE, AVR_INPUT_SWITCH_COLOR);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Today:", 25, 55, 4);
  tft.drawString("Week:", 25, 113, 4);
  tft.drawString("Month:", 25, 171, 4);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(minutes_to_str(time_switch_today_mins), 295, 55, 7);
  tft.drawString(minutes_to_str(time_switch_week_mins), 295, 113, 7);
  tft.drawString(minutes_to_str(time_switch_month_mins), 295, 171, 7);
  tft.setTextDatum(MC_DATUM);
}
// Displays the screen: Playing (PS5):
void tft_screen_playingPS5() {
  tft_backlight_set(SCREEN_ON_BRIGHTNESS);
  // Draw the clock section
  if (t_hour >= CLOCK_LATE_HOUR) tft.setTextColor(CLOCK_LATE_COLOR, TFT_BLACK);
  else if (t_hour >= CLOCK_VERYLATE_HOUR && t_hour < 6) tft.setTextColor(CLOCK_VERYLATE_COLOR, TFT_BLACK);
  else tft.setTextColor(CLOCK_DEFAULT_COLOR, TFT_BLACK);
  tft.drawString(clock_getTimeString(), 160, 55, 8);
  // Draw the game timer section:
  tft.fillRoundRect(20, 120, 280, 110, 20, AVR_INPUT_PS5_COLOR);
  tft.drawRoundRect(20, 120, 280, 110, 20, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(avr_input_ps5_n, 160, 145, 4);
  tft.setTextColor(TFT_WHITE, AVR_INPUT_PS5_COLOR);
  tft.drawString(minutes_to_str(time_ps5_today_mins), 160, 190, 7);
}
// Displays the screen: Playing (Nintendo Switch):
void tft_screen_playingSwitch() {
  tft_backlight_set(SCREEN_ON_BRIGHTNESS);
  // Draw the clock section
  if (t_hour >= 23) tft.setTextColor(CLOCK_LATE_COLOR, TFT_BLACK);
  else if (t_hour >= 0 && t_hour < 6) tft.setTextColor(CLOCK_VERYLATE_COLOR, TFT_BLACK);
  else tft.setTextColor(CLOCK_DEFAULT_COLOR, TFT_BLACK);
  tft.drawString(clock_getTimeString(), 160, 55, 8);
  // Draw the game timer section:
  tft.fillRoundRect(20, 120, 280, 110, 20, AVR_INPUT_SWITCH_COLOR);
  tft.drawRoundRect(20, 120, 280, 110, 20, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(avr_input_switch_n, 160, 145, 4);
  tft.setTextColor(TFT_WHITE, AVR_INPUT_SWITCH_COLOR);
  tft.drawString(minutes_to_str(time_switch_today_mins), 160, 190, 7);
}
// Displays the screen: Info:
void tft_screen_info() {
  tft_backlight_set(SCREEN_ON_BRIGHTNESS);
  tft.fillRoundRect(10, 5, 300, 230, 20, TFT_DARKGREY);
  tft.drawRoundRect(10, 5, 300, 230, 20, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Information", 160, 30, 4);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextDatum(TL_DATUM);

  // Left column
  tft.drawString("Wi-Fi SSID:", 25, 55, 2);
  tft.drawString("Wi-Fi RSSI:", 25, 75, 2);
  tft.drawString("IP:", 25, 95, 2);
  tft.drawString("Hostname:", 25, 115, 2);
  tft.drawString("AVR IP:", 25, 135, 2);
  tft.drawString("NTP time:", 25, 155, 2);
  tft.drawString("Version:", 25, 175, 2);
  tft.drawString("Build time:", 25, 195, 2);

  // Right column
  tft.drawString(WiFi.SSID(), 140, 55, 2);
  String rssi = String(WiFi.RSSI()) + " dBm";
  tft.drawString(rssi, 140, 75, 2);
  String ip = WiFi.localIP().toString();
  tft.drawString(ip, 140, 95, 2);
  tft.drawString(WiFi.hostname(), 140, 115, 2);
  tft.drawString(avr_ip_to_str(AVR_IP), 140, 135, 2);
  tft.drawString(ntp.formattedTime("%d/%d/%Y %T"), 140, 155, 2);
  tft.drawString(SOFTWARE_VERSION, 140, 175, 2);
  tft.drawString( __DATE__ " " __TIME__, 140, 195, 2);

  tft.setTextDatum(MC_DATUM);
}
// Handles the TFT on the loop:
void tft_handle() {
  // Repeat every 1 second or when forced:
  if (everySecond_tick < millis() || sm_screen_forceRefresh) {
    if (!sm_screen_forceRefresh) {
      everySecond_tick = millis() + 1000;
      tft_handle_nextScreen();
    }
    // Redraw clock when the minute changes or when forced
    if (t_minute_last != t_minute || sm_screen_forceRefresh) {
      if (!sm_screen_forceRefresh) t_minute_last = t_minute;
      if (sm_screen_forceRefresh) tft_clearScreen();
      // Display on screen
      tft.setTextDatum(MC_DATUM);
      switch(sm_screen) {
        case SM_SCREEN_OFF:
        case SM_SCREEN_IDLE:
          tft_screen_idle();
          break;
        case SM_SCREEN_INFO:
          tft_screen_info();
          //sm_nextScreen = SM_SCREEN_IDLE;
          //sm_timeToNextScreen = 5;
          break;
        case SM_SCREEN_STATS_ALL:
          tft_screen_StatsAll();
          sm_nextScreen = SM_SCREEN_IDLE;
          sm_timeToNextScreen = 5;
          break;
        case SM_SCREEN_STATS_PS5:
          tft_screen_StatsPS5();
          sm_nextScreen = SM_SCREEN_IDLE;
          sm_timeToNextScreen = 5;
          break;
        case SM_SCREEN_STATS_SWITCH:
          tft_screen_StatsSwitch();
          sm_nextScreen = SM_SCREEN_IDLE;
          sm_timeToNextScreen = 5;
          break;
        case SM_SCREEN_PLAY_PS5:
          tft_screen_playingPS5();
          break;
        case SM_SCREEN_PLAY_SWITCH:
          tft_screen_playingSwitch();
          break;
        default:
          // We display no clock in other screens
          break;
      } 
    }
  }
  if (sm_screen_forceRefresh) sm_screen_forceRefresh = false;
}

// --------------------------------------------------------------
// DENON AVR RELATED FUNCTIONS
// --------------------------------------------------------------
// Callback for the AVR connection when established:
void denon_connected(void* arg, AsyncClient* client) {
	Serial.printf("AVR: Connected at %s:%d\r\n", client->remoteIP().toString().c_str(), client->remotePort());
}
// Callback for the AVR connection when connection lost:
void denon_disconnected(void* arg, AsyncClient* client) {
	Serial.println("AVR: Disconnected");
  sm_screen = SM_SCREEN_IDLE;
  sm_screen_forceRefresh = true;
}
// Callback for the AVR when error occured:
void denon_conError(const char *errorMessage) {
  Serial.print("AVR: Error: ");
  Serial.println(errorMessage);
  sm_screen = SM_SCREEN_IDLE;
  sm_screen_forceRefresh = true;
}
// Callback for the AVR when there is a Message from connected AVR:
void denon_responded(const char *response, size_t len) {
  // Does nothing
}
// Callback for when the AVR power changes:
void denon_powerChanged(const char *response, size_t len) {
  Serial.print("AVR: Power ");
  if (strncmp(response, ON, len-1) == 0) {
    Serial.println("ON");
    // Load all data:
    fs_load_data_file();
    // Triggers the request of the selected input:
    DenonAvr.set(INPUT_SOURCE, "?");
  }
  else if (strncmp(response, OFF, len-1) == 0) {
    Serial.println("OFF");
    // Save all data:
    fs_save_data_file();
    // Show a status screen when we turn the AVR off, if we were playing:
    if (sm_screen == SM_SCREEN_PLAY_PS5)
      sm_screen = SM_SCREEN_STATS_PS5;
    else if (sm_screen == SM_SCREEN_PLAY_SWITCH)
      sm_screen = SM_SCREEN_STATS_SWITCH;
    else sm_screen = SM_SCREEN_IDLE;
    sm_screen_forceRefresh = true;
  }
}
// Callback for when the AVR input changes:
void denon_inputChanged(const char *response, size_t len) {
  // Save all data:
  fs_save_data_file();
  Serial.print("AVR: Input=");
  if (strncmp(response, AVR_INPUT_PS5, len-1) == 0) {
    Serial.println("PS5");
    sm_screen = SM_SCREEN_PLAY_PS5;
    sm_screen_forceRefresh = true;
  }
  else if (strncmp(response, AVR_INPUT_SWITCH, len-1) == 0) {
    Serial.println("Switch");
    sm_screen = SM_SCREEN_PLAY_SWITCH;
    sm_screen_forceRefresh = true;
  }
  else {
    Serial.write(response, len);
    Serial.println("");
    sm_screen = SM_SCREEN_IDLE;
    sm_screen_forceRefresh = true;
  }
}
// Initializes AVR service, callbacks and handlers:
void avr_init() {
  Serial.println("AVR: Connecting...");
  tft_message("AVR", "Connecting...");
  DenonAvr.onConnect(denon_connected);
  DenonAvr.onDisconnect(denon_disconnected);
  DenonAvr.onError(denon_conError);
  DenonAvr.onDenonResponse(denon_responded);
  DenonAvr_Power->onStateUpdate(denon_powerChanged);
  DenonAvr_Input->onStateUpdate(denon_inputChanged);
  if (DenonAvr.begin(DENON_IP)) {
    Serial.println("AVR: Connected");
    tft_message("AVR", "Connected!");
  }
  else {
    Serial.println("AVR: ERROR: Could not connect");
    tft_message("AVR", "Connection error!");
  }
}

// --------------------------------------------------------------
// NTP RELATED FUNCTIONS
// --------------------------------------------------------------
// Initializes NTP service:
void ntp_init() {
  Serial.println("NTP: Starting...");
  tft_message("NTP", "Connecting to server...");
  ntp.ruleDST("CEST", Last, Sun, Mar, 2, 120); // last sunday in march 2:00, timetone +120min (+1 GMT + 1h summertime offset)
  ntp.ruleSTD("CET", Last, Sun, Oct, 3, 60); // last sunday in october 3:00, timezone +60min (+1 GMT)
  ntp.begin();
  Serial.println("NTP: Started");
  tft_message("NTP", "Service started!");
}
// Handle update local time variables:
void ntp_handle() {
  // NOTE: Every 10 seconds is enough since we dont care about displaying seconds
  if (ntpUpdate_tick < millis()) {
    ntpUpdate_tick = millis() + 10000;

    ntp.update();
    t_hour = ntp.hours();
    t_minute = ntp.minutes();
    t_day = ntp.day();
    t_month = ntp.month();
    t_year = ntp.year();
    t_weekday = ntp.weekDay();
    //Serial.println(ntp.formattedTime("%d. %B %Y")); // dd. Mmm yyyy
    //Serial.println(ntp.formattedTime("%A %T")); // Www hh:mm:ss
  }
}

// --------------------------------------------------------------
// OTA RELATED FUNCTIONS
// --------------------------------------------------------------
// Initializes ArduinoOTA service:
void ota_arduino_init() {
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(host_name);
  // No authentication by default
  // ArduinoOTA.setPassword("admin");
  // Password can be set with it's md5 value as well: MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("OTA: Update started..." + type);
    tft_message("OTA", "Update started... " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("");
    Serial.println("OTA: Update finished");
    tft_message("OTA", "Update finished");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA: Error: [%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
      tft_message("OTA", "Error: Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
      tft_message("OTA", "Error: Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
      tft_message("OTA", "Error: Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
      tft_message("OTA", "Error: Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
      tft_message("OTA", "Error: End Failed");
    }
  });
  ArduinoOTA.begin();
}
// Initializes WebServer OTA service:
void ota_webserver_init() {
  MDNS.begin(host_name);

  httpUpdater.setup(&httpServer);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
  Serial.printf("OTA: Webserver ready at http://%s.local/update\n", host_name);
}
// Initializes all OTA services:
void ota_init() {
  Serial.println("OTA: Starting...");
  tft_message("OTA", "Initializing...");

  ota_arduino_init();
  ota_webserver_init();

  Serial.println("OTA: Service started");
  tft_message("OTA", "Service started!");
}
// Handles all OTA services:
void ota_handle() {
  ArduinoOTA.handle();
  httpServer.handleClient();
  MDNS.update();
}

// ==============================================================
// SETUP
// ==============================================================
void setup() {
  serial_init();
  fs_init();
  tft_init();
  tft_touch_init();
  wifi_init();
  ota_init();
  ntp_init();
  avr_init();
  time_init();
}

// ==============================================================
// LOOP
// ==============================================================
void loop() {
  time_handle();
  tft_handle();
  tft_touch_handle();
  ntp_handle();
  ota_handle();
}
