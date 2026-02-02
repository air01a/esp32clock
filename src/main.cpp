#include <WiFi.h>
#include "time.h"
#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>


// === CONFIGURATION ===
// ST7789 screen pins
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST   4
#define TFT_BLK   5

// Wi-Fi + NTP
const char* WIFI_SSID = "goaway";
const char* WIFI_PASSWORD = "mdp$Ax01SUPadv@";
const char* NTP_SERVER = "pool.ntp.org";
const char* TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";

// Day/Night parameters
const int DAY_START_H = 9;    
const int DAY_START_M = 00;    
const int DAY_END_H = 19;      
const int DAY_END_M = 45;

// Display options
const bool SHOW_DAY_INSTEAD_OF_DATE = false;
const bool IS_24H_FORMAT = false;

// Custom colors (RGB565)
#define COLOR_SKY_DAY    0x5D9F
#define COLOR_SKY_NIGHT  0x1884
#define COLOR_SUN        0xFE60
#define COLOR_MOON       0xFFFF
#define COLOR_TIME       0xFFE0
#define COLOR_STARS      0xFFFF
#define COLOR_SCHOOL     0xFDA0
#define COLOR_OFFDAY     0x87F0
#define COLOR_SHADOW     0x18C3

// === GLOBAL OBJECTS ===
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
RTC_DS3231 rtc;

// State variables
int lastHour = -1;
int lastMinute = -1;
int lastDay = -1;
int lastWasNight = -1;
unsigned long lastNTPSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 7UL * 24 * 60 * 60 * 1000;

// Zone de l'heure pour optimisation
struct TimeDisplayArea {
  int16_t x, y, w, h;
} timeArea = {0, 0, 0, 0};

// === GRAPHIC FUNCTIONS ===

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

void drawMoon(int x, int y, int radius) {
  tft.fillCircle(x, y, radius, COLOR_MOON);
  tft.fillCircle(x + 12, y - 8, radius, COLOR_SKY_NIGHT);
  tft.fillCircle(x - 8, y - 5, 4, 0xCE59);
  tft.fillCircle(x + 5, y + 8, 3, 0xCE59);
  tft.fillCircle(x - 2, y + 10, 2, 0xCE59);
}

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

// 🕒 Time display optimisé
void displayTime(int hour, int minute, bool forceRedraw = false) {
  static char lastBuffer[6] = "";
  char buffer[6];
  
  if (IS_24H_FORMAT) {
    sprintf(buffer, "%02d:%02d", hour, minute);
  } else {
    int displayHour = hour % 12;
    if (displayHour == 0) displayHour = 12;
    sprintf(buffer, "%02d:%02d", displayHour, minute);
  }

  if (strcmp(buffer, lastBuffer) == 0 && !forceRedraw) {
    return;
  }
  strcpy(lastBuffer, buffer);

  bool isNight = lastWasNight == 1;
  uint16_t bgColor = isNight ? COLOR_SKY_NIGHT : COLOR_SKY_DAY;

  tft.setFont(&FreeSansBold24pt7b);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(buffer, 0, 0, &x1, &y1, &w, &h);
  
  // Position centrale avec baseline correcte
  int xPos = (240 - w) / 2 - x1;
  int yPos = 130;  // Baseline position

  // Effacer l'ancienne zone si nécessaire
  if (timeArea.w > 0 || forceRedraw) {
    // Calcul de la zone totale incluant l'ombre
    int clearX = min(xPos + x1 - 5, xPos + x1 + 2);
    int clearY = yPos + y1 - 5;
    int clearW = w + 15;
    int clearH = h + 15;
    
    tft.fillRect(clearX, clearY, clearW, clearH, bgColor);
  }

  // Dessiner l'ombre (décalage de 2px au lieu de 3 pour plus de finesse)
  tft.setTextColor(COLOR_SHADOW);
  tft.setCursor(xPos + 2, yPos + 2);
  tft.print(buffer);

  // Dessiner le texte principal
  tft.setTextColor(COLOR_TIME);
  tft.setCursor(xPos, yPos);
  tft.print(buffer);

  // Sauvegarder la zone pour la prochaine fois
  timeArea.x = xPos + x1 - 5;
  timeArea.y = yPos + y1 - 5;
  timeArea.w = w + 15;
  timeArea.h = h + 15;
  
  tft.setFont();
}

// 📅 Day or date display optimisé
void displayDayOrDate(int dayOfWeek, int day, int month, int year, bool forceRedraw = false) {
  static char lastBuffer[20] = "";
  static int16_t lastX = 0, lastY = 0, lastW = 0, lastH = 0;
  
  const char* days[] = {"Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};
  const char* months[] = {"", "Jan", "Fev", "Mars", "Avril", "Mai", "Juin",
                          "Jui", "Aout", "Sep", "Oct", "Nov", "Dec"};
  const char* daysShort[] = {"Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"};

  char buffer[20];
  uint16_t color;
  bool isNoSchoolDay = (dayOfWeek == 0 || dayOfWeek == 3 || dayOfWeek == 6);

  if (SHOW_DAY_INSTEAD_OF_DATE) {
    sprintf(buffer, "%s", days[dayOfWeek]);
    color = isNoSchoolDay ? COLOR_OFFDAY : COLOR_SCHOOL;
  } else {
    sprintf(buffer, "%s %d %s", daysShort[dayOfWeek], day, months[month]);
    color = isNoSchoolDay ? COLOR_OFFDAY : COLOR_SCHOOL;
  }

  if (strcmp(buffer, lastBuffer) == 0 && !forceRedraw) {
    return;
  }
  strcpy(lastBuffer, buffer);

  bool isNight = lastWasNight == 1;
  uint16_t bgColor = isNight ? COLOR_SKY_NIGHT : COLOR_SKY_DAY;

  tft.setFont(&FreeMono9pt7b);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(buffer, 0, 0, &x1, &y1, &w, &h);
  
  int16_t xPos = (240 - w) / 2 - x1;
  int16_t yPos = 190;

  // Effacer l'ancienne zone si elle existait
  if (lastW > 0 || forceRedraw) {
    uint16_t clearW = (lastW > w ? lastW : w) + 10;
    int16_t clearX = (lastX < xPos + x1 ? lastX : xPos + x1) - 5;
    tft.fillRect(clearX, yPos + y1 - 5, clearW, h + 10, bgColor);
  }

  tft.setTextColor(color);
  tft.setCursor(xPos, yPos);
  tft.print(buffer);

  // Sauvegarder pour la prochaine fois
  lastX = xPos + x1;
  lastY = yPos + y1;
  lastW = w;
  lastH = h;
  
  tft.setFont();
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

void loop() {
  DateTime now = rtc.now();
  int hour = now.hour();
  int minute = now.minute();
  int day = now.day();

  // Weekly NTP resync
  if (hour == 3 && minute == 0 && (millis() - lastNTPSync > NTP_SYNC_INTERVAL)) {
    if (syncTimeWithNTP()) {
      lastNTPSync = millis();
      lastHour = -1;
      lastMinute = -1;
      lastDay = -1;
      lastWasNight = -1;
      timeArea = {0, 0, 0, 0};
    }
  }

  // Détection du changement jour/nuit
  bool isNight = (hour < DAY_START_H) ||
                 (hour > DAY_END_H) ||
                 (hour == DAY_START_H && minute < DAY_START_M) ||
                 (hour == DAY_END_H && minute >= DAY_END_M) ||
                  (hour > 12) && ((hour < 16) || (hour == 16 && minute < 30));

  bool needsFullRedraw = false;

  // Transition jour/nuit complète
  if (isNight != lastWasNight) {
    lastWasNight = isNight;
    needsFullRedraw = true;
    
    tft.fillScreen(isNight ? COLOR_SKY_NIGHT : COLOR_SKY_DAY);
    
    if (isNight) {
      drawStars();
      drawMoon(120, 250, 30);
      ledcWrite(0, 1);
    } else {
      drawSun(120, 250, 28);
      ledcWrite(0, 250);
    }
    
    // Reset time area pour forcer le redessinage
    timeArea = {0, 0, 0, 0};
  }

  // Mise à jour de l'heure
  if (hour != lastHour || minute != lastMinute || needsFullRedraw) {
    lastHour = hour;
    lastMinute = minute;
    displayTime(hour, minute, needsFullRedraw);
    
    Serial.printf("🕐 %02d:%02d | %s\n", hour, minute, isNight ? "🌙 Night" : "☀️ Day");
  }

  // Mise à jour de la date
  if (day != lastDay || needsFullRedraw) {
    lastDay = day;
    displayDayOrDate(now.dayOfTheWeek(), now.day(), now.month(), now.year(), needsFullRedraw);
  }

  delay(1000);
}