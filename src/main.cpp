#include <WiFi.h>
#include "time.h"
#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// === CONFIGURATION ===
// ST7789 screen pins
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST   4
#define TFT_BLK   5   // backlight control pin (PWM)

// Wi-Fi + NTP
const char* WIFI_SSID = "XXXXX";
const char* WIFI_PASSWORD = "XXXX";
const char* NTP_SERVER = "pool.ntp.org";
const char* TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3"; // Europe/Paris with DST

// Day/Night parameters
const int DAY_START_H = 8;    
const int DAY_START_M = 00;    
const int DAY_END_H = 20;      
const int DAY_END_M = 30;

// Display options
const bool SHOW_DAY_INSTEAD_OF_DATE = true;

// Custom colors (RGB565)
#define COLOR_SKY_DAY    0x5D9F  // Light blue
#define COLOR_SKY_NIGHT  0x1884  // Dark blue
#define COLOR_SUN        0xFE60  // Orange-yellow
#define COLOR_MOON       0xFFFF  // White
#define COLOR_TIME       0xFFE0  // Light yellow
#define COLOR_STARS      0xFFFF  // White
#define COLOR_SCHOOL     0xFDA0  // Orange
#define COLOR_OFFDAY     0x87F0  // Light green

// === GLOBAL OBJECTS ===
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
RTC_DS3231 rtc;

// State variables
int lastHour = -1;
int lastMinute = -1;
int lastWasNight = -1;
unsigned long lastNTPSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 7UL * 24 * 60 * 60 * 1000; // 7 days

// === GRAPHIC FUNCTIONS ===

// ☀️ Sun
void drawSun(int x, int y, int r) {
  tft.fillCircle(x, y, r, COLOR_SUN);
  for (int i = 0; i < 12; i++) {
    float angle = i * 30 * 3.14159 / 180;
    int x1 = x + cos(angle) * (r + 10);
    int y1 = y + sin(angle) * (r + 10);
    int x2 = x + cos(angle) * (r + 10 + 5 * (i % 2));
    int y2 = y + sin(angle) * (r + 10 + 5 * (i % 2));
    tft.drawLine(x1, y1, x2, y2, COLOR_SUN);
  }
}

// 🌙 Moon
void drawMoon(int x, int y, int radius) {
  tft.fillCircle(x, y, radius, COLOR_MOON);
  tft.fillCircle(x + 12, y - 8, radius, COLOR_SKY_NIGHT);
  tft.fillCircle(x - 8, y - 5, 4, 0xCE59);
  tft.fillCircle(x + 5, y + 8, 3, 0xCE59);
  tft.fillCircle(x - 2, y + 10, 2, 0xCE59);
}

// ⭐ Stars
void drawStars() {
  int stars[][2] = {
    {20, 30}, {80, 50}, {150, 25}, {200, 60},
    {40, 80}, {180, 90}, {100, 100}, {220, 110},
    {30, 250}, {190, 240}, {70, 270}, {160, 290}
  };
  for (int i = 0; i < 12; i++) {
    int size = (i % 3) + 1;
    tft.fillCircle(stars[i][0], stars[i][1], size, COLOR_STARS);
  }
}

// 🕒 Time centered
void displayTime(int hour, int minute) {
  char buffer[6];
  sprintf(buffer, "%02d:%02d", hour, minute);
  tft.setTextSize(6);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(buffer, 0, 0, &x1, &y1, &w, &h);
  int xPos = (240 - w) / 2;
  int yPos = 100;

  // Shadow
  tft.setTextColor(0x18C3);
  tft.setCursor(xPos + 3, yPos + 3);
  tft.print(buffer);

  // Main text
  tft.setTextColor(COLOR_TIME);
  tft.setCursor(xPos, yPos);
  tft.print(buffer);
}

// 📅 Day or date display
void displayDayOrDate(int dayOfWeek, int day, int month, int year) {
  const char* days[] = {"Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};
  const char* months[] = {"", "Jan", "Fev", "Mars", "Avril", "Mai", "Juin",
                          "Jui", "Aout", "Sep", "Oct", "Nov", "Dec"};

  char buffer[20];
  uint16_t color;

  if (SHOW_DAY_INSTEAD_OF_DATE) {
    sprintf(buffer, "%s", days[dayOfWeek]);
    // School days = Monday, Tuesday, Thursday, Friday
    bool isNoSchoolDay = (dayOfWeek == 0 || dayOfWeek == 3 || dayOfWeek == 6); // Sun, Wed, Sat
    color = isNoSchoolDay ? COLOR_OFFDAY : COLOR_SCHOOL;
  } else {
    sprintf(buffer, "%d %s %d", day, months[month], year);
    color = 0xAD55;
  }

  tft.setTextSize(2);
  tft.setTextColor(color);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(buffer, 0, 0, &x1, &y1, &w, &h);
  int xPos = (240 - w) / 2;

  tft.setCursor(xPos, 160);
  tft.print(buffer);
}

// === NTP SYNC ===
bool syncTimeWithNTP(bool showMessages = true) {
  if (showMessages) {
    tft.fillScreen(0x0000);
    tft.setTextSize(2);
    tft.setTextColor(0xFFFF);
    tft.setCursor(30, 140);
    tft.print("Connecting WiFi...");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Wi-Fi connecting");

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(300);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠️ Wi-Fi failed");
    return false;
  }

  Serial.println("\n✅ Wi-Fi connected");
  configTime(0, 0, NTP_SERVER);
  setenv("TZ", TIMEZONE, 1);
  tzset();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5000)) {
    Serial.println("❌ NTP error");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  rtc.adjust(DateTime(
    timeinfo.tm_year + 1900,
    timeinfo.tm_mon + 1,
    timeinfo.tm_mday,
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  ));

  Serial.printf("✅ Synced time: %02d:%02d:%02d\n",
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return true;
}

// === SETUP ===
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 ST7789 Clock ===");

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  Wire.begin();

  if (!rtc.begin()) {
    Serial.println("❌ RTC not detected!");
    while (1) delay(10);
  }

  ledcAttachPin(TFT_BLK, 0);
  ledcSetup(0, 5000, 8);
  ledcWrite(0, 1);

  tft.init(240, 320);
  tft.setRotation(0);
  tft.invertDisplay(false);
  tft.sendCommand(ST77XX_MADCTL);
  tft.spiWrite(ST77XX_MADCTL_MX | 0x08);
  tft.sendCommand(ST77XX_COLMOD);
  tft.spiWrite(0x55);
  tft.fillScreen(0x0000);

  if (!syncTimeWithNTP()) {
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  } else {
    lastNTPSync = millis();
  }

  Serial.println("✅ Initialization complete\n");
}

// === LOOP ===
void loop() {
  DateTime now = rtc.now();
  int hour = now.hour();
  int minute = now.minute();

  // Weekly NTP resync (at 3 AM)
  if (hour == 3 && minute == 0 && (millis() - lastNTPSync > NTP_SYNC_INTERVAL)) {
    if (syncTimeWithNTP()) {
      lastNTPSync = millis();
      lastHour = -1;
      lastMinute = -1;
    }
  }

  if (hour != lastHour || minute != lastMinute) {
    lastHour = hour;
    lastMinute = minute;

    bool isNight = (hour < DAY_START_H) ||
                   (hour > DAY_END_H) ||
                   (hour == DAY_START_H && minute < DAY_START_M) ||
                   (hour == DAY_END_H && minute >= DAY_END_M);

    if (isNight != lastWasNight) {
      lastWasNight = isNight;
      tft.fillScreen(isNight ? COLOR_SKY_NIGHT : COLOR_SKY_DAY);
      if (isNight) {
        drawStars();
        ledcWrite(0, 1);
      } else {
        ledcWrite(0, 250);
      }
    } else {
      tft.fillRect(0, 80, 240, 80, isNight ? COLOR_SKY_NIGHT : COLOR_SKY_DAY);
      tft.fillRect(0, 200, 240, 100, isNight ? COLOR_SKY_NIGHT : COLOR_SKY_DAY);
    }

    displayTime(hour, minute);
    displayDayOrDate(now.dayOfTheWeek(), now.day(), now.month(), now.year());
    if (isNight) drawMoon(120, 250, 30);
    else drawSun(120, 250, 28);

    Serial.printf("🕐 %02d:%02d | %s\n", hour, minute, isNight ? "🌙 Night" : "☀️ Day");
  }

  delay(1000);
}
