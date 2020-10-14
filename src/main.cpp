#include <Arduino.h>
#include <M5Stack.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "time.h"
#include "externs.h"
#include <FS.h>
// #include <util/eu_dst.h>
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>
#include "Free_Fonts.h"
#include "IniFile.h"
#include "M5NSconfig.h"
#include "M5NSWebConfig.h"
#include "DHT12.h"
#include <Wire.h> //The DHT12 uses I2C comunication.
#include "WiFiConfig.h"

DHT12 dht12; //Preset scale CELSIUS and ID 0x5c.

String M5NSversion("20200825");

// extern const unsigned char alarmSndData[];

extern const unsigned char sun_icon16x16[];
extern const unsigned char clock_icon16x16[];
extern const unsigned char timer_icon16x16[];
extern const unsigned char powerbutton_icon16x16[];
extern const unsigned char door_icon16x16[];
extern const unsigned char warning_icon16x16[];
extern const unsigned char wifi1_icon16x16[];
extern const unsigned char wifi2_icon16x16[];

extern const unsigned char bat0_icon16x16[];
extern const unsigned char bat1_icon16x16[];
extern const unsigned char bat2_icon16x16[];
extern const unsigned char bat3_icon16x16[];
extern const unsigned char bat4_icon16x16[];
extern const unsigned char plug_icon16x16[];

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

uint16_t osx = 120, osy = 120, omx = 120, omy = 120, ohx = 120, ohy = 120; // Saved H, M, S x & y coords
boolean initial = 1;
boolean mDNSactive = false;
struct NSinfo ns;
Preferences preferences;
tConfig cfg;

int dispPage = 0;
#define MAX_PAGE 3
int maxPage = MAX_PAGE;

// -- Callback method declarations

unsigned long msCount;
unsigned long msCountLog;
unsigned long msStart;
uint8_t lcdBrightness = 10;
const char *iniFilename = "/M5NS.INI";

DynamicJsonDocument JSONdoc(8192);
time_t lastAlarmTime = 0;
time_t lastSnoozeTime = 0;
static uint8_t music_data[25000];

int icon_xpos[3] = {144, 144 + 18, 144 + 2 * 18};
int icon_ypos[3] = {0, 0, 0};

struct Config
{
  char tomatoShareID[128] = {0};
  bool nSenabled;
  char nSserver[128] = {0};
  char token[128] = {0};
  int port = 0;
  int dataSource = 1;
  int timezone = 21;
  float warningLow = 4;
  float warningHigh = 10;
  float alarmLow = 3.9;
  float alarmHigh = 13.9;
} config;

ConfigManager configManager;

const char *ntpServer = "pool.ntp.org"; // "time.nist.gov", "time.google.com"
struct tm localTimeInfo;
int MAX_TIME_RETRY = 30;
int lastSec = 61;
int lastMin = 61;
char localTimeStr[30];

struct err_log_item
{
  struct tm err_time;
  int err_code;
} err_log[10];
int err_log_ptr = 0;
int err_log_count = 0;

void printLocalTime()
{
  if (!getLocalTime(&localTimeInfo))
  {
    Serial.println("Failed to obtain time");
    M5.Lcd.println("Failed to obtain time");
    return;
  }
  Serial.println(&localTimeInfo, "%A, %B %d %Y %H:%M:%S");
  M5.Lcd.println(&localTimeInfo, "%A, %B %d %Y %H:%M:%S");
}

void getDevicetime()
{
  cfg.timeZone = config.timezone * 60 * 60;
  configTime(cfg.timeZone, cfg.dst, ntpServer, "time.nist.gov", "time.google.com");
  delay(1000);
  Serial.print("Waiting for time.");
  int i = 0;
  while (!getLocalTime(&localTimeInfo))
  {
    Serial.print(".");
    delay(1000);
    i++;
    if (i > MAX_TIME_RETRY)
    {
      Serial.print("Gave up waiting for time to have a valid value.");
      break;
    }
  }
  Serial.println();
  printLocalTime();

  Serial.println("Connection done");
  M5.Lcd.println("Connection done");
  return;
}

void play_music_data(uint32_t data_length, uint8_t volume)
{
  uint8_t vol;
  if (volume > 100)
    vol = 1;
  else
    vol = 101 - volume;
  if (vol != 101)
  {
    ledcSetup(TONE_PIN_CHANNEL, 0, 13);
    ledcAttachPin(SPEAKER_PIN, TONE_PIN_CHANNEL);
    delay(10);
    for (int i = 0; i < data_length; i++)
    {
      dacWrite(SPEAKER_PIN, music_data[i] / vol);
      delayMicroseconds(194); // 200 = 1 000 000 microseconds / sample rate 5000
    }
    /* takes too long
    // slowly set DAC to zero from the last value
    for(int t=music_data[data_length-1]; t>=0; t--) {
      dacWrite(SPEAKER_PIN, t);
      delay(2);
    } */
    for (int t = music_data[data_length - 1] / vol; t >= 0; t--)
    {
      dacWrite(SPEAKER_PIN, t);
      delay(2);
    }
    // dacWrite(SPEAKER_PIN, 0);
    // delay(10);
    ledcAttachPin(SPEAKER_PIN, TONE_PIN_CHANNEL);
    ledcWriteTone(TONE_PIN_CHANNEL, 0);
    CLEAR_PERI_REG_MASK(RTC_IO_PAD_DAC1_REG, RTC_IO_PDAC1_XPD_DAC | RTC_IO_PDAC1_DAC_XPD_FORCE);
  }
}

void play_tone(uint16_t frequency, uint32_t duration, uint8_t volume)
{
  // Serial.print("start fill music data "); Serial.println(millis());
  uint32_t data_length = 5000;
  if (duration * 5 < data_length)
    data_length = duration * 5;
  float interval = 2 * M_PI * float(frequency) / float(5000);
  for (int i = 0; i < data_length; i++)
  {
    music_data[i] = 127 + 126 * sin(interval * i);
  }
  // Serial.print("finish fill music data "); Serial.println(millis());
  play_music_data(data_length, volume);
}

void drawIcon(int16_t x, int16_t y, const uint8_t *bitmap, uint16_t color)
{
  int16_t w = 16;
  int16_t h = 16;
  int32_t i, j, byteWidth = (w + 7) / 8;
  for (j = 0; j < h; j++)
  {
    for (i = 0; i < w; i++)
    {
      if (pgm_read_byte(bitmap + j * byteWidth + i / 8) & (128 >> (i & 7)))
      {
        M5.Lcd.drawPixel(x + i, y + j, color);
      }
    }
  }
}

void sndAlarm()
{
  for (int j = 0; j < 6; j++)
  {
    if (cfg.dev_mode)
      play_tone(660, 400, 1);
    else
      play_tone(660, 400, cfg.alarm_volume);
    delay(200);
  }
}

void sndWarning()
{
  for (int j = 0; j < 3; j++)
  {
    if (cfg.dev_mode)
      play_tone(3000, 100, 1);
    else
      play_tone(3000, 100, cfg.warning_volume);
    delay(300);
  }
}

void addErrorLog(int code)
{
  if (err_log_ptr > 9)
  {
    for (int i = 0; i < 9; i++)
    {
      err_log[i].err_time = err_log[i + 1].err_time;
      err_log[i].err_code = err_log[i + 1].err_code;
    }
    err_log_ptr = 9;
  }
  getLocalTime(&err_log[err_log_ptr].err_time);
  err_log[err_log_ptr].err_code = code;
  err_log_ptr++;
  err_log_count++;
}

void handleAlarmsInfoLine(struct NSinfo *ns)
{
  struct tm timeinfo;

  // calculate sensor time difference
  // calculate last alarm time difference
  int sensorDifSec = 24 * 60 * 60;            // too much
  int alarmDifSec = 24 * 60 * 60;             // too much
  int snoozeDifSec = cfg.snooze_timeout * 60; // timeout
  if (getLocalTime(&timeinfo))
  {
    sensorDifSec = difftime(mktime(&timeinfo), ns->sensTime);
    alarmDifSec = difftime(mktime(&timeinfo), lastAlarmTime);
    snoozeDifSec = difftime(mktime(&timeinfo), lastSnoozeTime);
    if (snoozeDifSec > cfg.snooze_timeout * 60)
      snoozeDifSec = cfg.snooze_timeout * 60; // timeout
  }
  unsigned int sensorDifMin = (sensorDifSec + 30) / 60;

  Serial.print("Alarm time difference = ");
  Serial.print(alarmDifSec);
  Serial.println(" sec");
  Serial.print("Snooze time difference = ");
  Serial.print(snoozeDifSec);
  Serial.println(" sec");
  char tmpStr[10];
  M5.Lcd.setTextDatum(TL_DATUM);
  if (snoozeDifSec < cfg.snooze_timeout * 60)
  {
    sprintf(tmpStr, "%i", (cfg.snooze_timeout * 60 - snoozeDifSec + 59) / 60);
    if (dispPage < maxPage)
      drawIcon(icon_xpos[1], icon_ypos[1], (uint8_t *)clock_icon16x16, TFT_RED);
  }
  else
  {
    strcpy(tmpStr, "Snooze");
    if (dispPage < maxPage)
      M5.Lcd.fillRect(icon_xpos[1], icon_ypos[1], 16, 16, BLACK);
  }
  M5.Lcd.setTextSize(1);
  M5.Lcd.setFreeFont(FSSB12);

  /**
   * TODO:
   * - when use tomato, gert alarm via range 
  */
  if ((ns->sensSgv <= cfg.snd_alarm) && (ns->sensSgv >= 0.1))
  {
    // red alarm state
    // M5.Lcd.fillRect(110, 220, 100, 20, TFT_RED);
    Serial.println("ALARM LOW");
    M5.Lcd.fillRect(0, 220, 320, 20, TFT_RED);
    M5.Lcd.setTextColor(TFT_BLACK, TFT_RED);
    int stw = M5.Lcd.textWidth(tmpStr);
    M5.Lcd.drawString(tmpStr, 159 - stw / 2, 220, GFXFF);
    if ((alarmDifSec > cfg.alarm_repeat * 60) && (snoozeDifSec == cfg.snooze_timeout * 60))
    {
      sndAlarm();
      lastAlarmTime = mktime(&timeinfo);
    }
  }
  else
  {
    if ((ns->sensSgv <= cfg.snd_warning) && (ns->sensSgv >= 0.1))
    {
      // yellow warning state
      // M5.Lcd.fillRect(110, 220, 100, 20, TFT_YELLOW);
      Serial.println("WARNING LOW");
      M5.Lcd.fillRect(0, 220, 320, 20, TFT_YELLOW);
      M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
      int stw = M5.Lcd.textWidth(tmpStr);
      M5.Lcd.drawString(tmpStr, 159 - stw / 2, 220, GFXFF);
      if ((alarmDifSec > cfg.alarm_repeat * 60) && (snoozeDifSec == cfg.snooze_timeout * 60))
      {
        sndWarning();
        lastAlarmTime = mktime(&timeinfo);
      }
    }
    else
    {
      if (ns->sensSgv >= cfg.snd_alarm_high)
      {
        // red alarm state
        // M5.Lcd.fillRect(110, 220, 100, 20, TFT_RED);
        Serial.println("ALARM HIGH");
        M5.Lcd.fillRect(0, 220, 320, 20, TFT_RED);
        M5.Lcd.setTextColor(TFT_BLACK, TFT_RED);
        int stw = M5.Lcd.textWidth(tmpStr);
        M5.Lcd.drawString(tmpStr, 159 - stw / 2, 220, GFXFF);
        if ((alarmDifSec > cfg.alarm_repeat * 60) && (snoozeDifSec == cfg.snooze_timeout * 60))
        {
          sndAlarm();
          lastAlarmTime = mktime(&timeinfo);
        }
      }
      else
      {
        if (ns->sensSgv >= cfg.snd_warning_high)
        {
          // yellow warning state
          // M5.Lcd.fillRect(110, 220, 100, 20, TFT_YELLOW);
          Serial.println("WARNING HIGH");
          M5.Lcd.fillRect(0, 220, 320, 20, TFT_YELLOW);
          M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
          int stw = M5.Lcd.textWidth(tmpStr);
          M5.Lcd.drawString(tmpStr, 159 - stw / 2, 220, GFXFF);
          if ((alarmDifSec > cfg.alarm_repeat * 60) && (snoozeDifSec == cfg.snooze_timeout * 60))
          {
            sndWarning();
            lastAlarmTime = mktime(&timeinfo);
          }
        }
        else
        {
          if (sensorDifMin >= cfg.snd_no_readings)
          {
            // yellow warning state
            // M5.Lcd.fillRect(110, 220, 100, 20, TFT_YELLOW);
            Serial.println("WARNING NO READINGS");
            M5.Lcd.fillRect(0, 220, 320, 20, TFT_YELLOW);
            M5.Lcd.setTextColor(TFT_BLACK, TFT_YELLOW);
            int stw = M5.Lcd.textWidth(tmpStr);
            M5.Lcd.drawString(tmpStr, 159 - stw / 2, 220, GFXFF);
            if ((alarmDifSec > cfg.alarm_repeat * 60) && (snoozeDifSec == cfg.snooze_timeout * 60))
            {
              sndWarning();
              lastAlarmTime = mktime(&timeinfo);
            }
          }
          else
          {
            // normal glycemia state
            M5.Lcd.fillRect(0, 220, 320, 20, TFT_BLACK);
            M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            // draw info line
            char infoStr[64];
            switch (cfg.info_line)
            {
            case 0: // sensor information
              strcpy(infoStr, ns->sensDev);
              if (strcmp(infoStr, "MIAOMIAO") == 0)
              {
                if (ns->is_xDrip)
                {
                  strcpy(infoStr, "xDrip MiaoMiao + Libre");
                }
                else
                {
                  strcpy(infoStr, "Spike MiaoMiao + Libre");
                }
              }
              if (strcmp(infoStr, "Tomato") == 0)
                strcat(infoStr, " MiaoMiao + Libre");
              M5.Lcd.drawString(infoStr, 0, 220, GFXFF);
              break;
            case 1: // button function icons
              drawIcon(58, 220, (uint8_t *)sun_icon16x16, TFT_LIGHTGREY);
              drawIcon(153, 220, (uint8_t *)clock_icon16x16, TFT_LIGHTGREY);
              // drawIcon(153, 220, (uint8_t*)timer_icon16x16, TFT_LIGHTGREY);
              drawIcon(246, 220, (uint8_t *)door_icon16x16, TFT_LIGHTGREY);
              break;
            case 2: // loop + basal information
              strcpy(infoStr, "L: ");
              strlcat(infoStr, ns->loop_display_label, 64);
              M5.Lcd.drawString(infoStr, 0, 220, GFXFF);
              strcpy(infoStr, "B: ");
              strlcat(infoStr, ns->basal_display, 64);
              M5.Lcd.drawString(infoStr, 160, 220, GFXFF);
              break;
            }
          }
        }
      }
    }
  }
}

void buttons_test()
{
  if (M5.BtnA.wasPressed())
  {
    // M5.Lcd.printf("A");
    Serial.printf("A");
    // play_tone(1000, 10, 1);
    // sndAlarm();
    if (lcdBrightness == cfg.brightness1)
      lcdBrightness = cfg.brightness2;
    else if (lcdBrightness == cfg.brightness2)
      lcdBrightness = cfg.brightness3;
    else
      lcdBrightness = cfg.brightness1;
    M5.Lcd.setBrightness(lcdBrightness);
    // addErrorLog(500);
  }
  if (M5.BtnB.wasPressed())
  {
    // M5.Lcd.printf("B");
    Serial.printf("B");
    /*
    play_tone(440, 100, 1);
    delay(10);
    play_tone(880, 100, 1);
    delay(10);
    play_tone(1760, 100, 1);
    */
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
      lastSnoozeTime = 0;
    }
    else
    {
      lastSnoozeTime = mktime(&timeinfo);
    }
    M5.Lcd.fillRect(110, 220, 100, 20, TFT_WHITE);
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setFreeFont(FSSB12);
    M5.Lcd.setTextColor(TFT_BLACK, TFT_WHITE);
    char tmpStr[10];
    sprintf(tmpStr, "%i", cfg.snooze_timeout);
    int txw = M5.Lcd.textWidth(tmpStr);
    Serial.print("Set SNOOZE: ");
    Serial.println(tmpStr);
    M5.Lcd.drawString(tmpStr, 159 - txw / 2, 220, GFXFF);
    if (dispPage < maxPage)
      drawIcon(icon_xpos[1], icon_ypos[1], (uint8_t *)clock_icon16x16, TFT_RED);
  }

  if (M5.BtnC.wasPressed())
  {
    // M5.Lcd.printf("C");
    Serial.printf("C");
    unsigned long btnCPressTime = millis();
    long pwrOffTimeout = 4000;
    int lastDispTime = pwrOffTimeout / 1000;
    int longPress = 0;
    char tmpstr[32];
    while (M5.BtnC.read())
    {
      M5.Lcd.setTextSize(1);
      M5.Lcd.setFreeFont(FSSB12);
      // M5.Lcd.fillRect(110, 220, 100, 20, TFT_RED);
      // M5.Lcd.fillRect(0, 220, 320, 20, TFT_RED);
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      int timeToPwrOff = (pwrOffTimeout - (millis() - btnCPressTime)) / 1000;
      if ((lastDispTime != timeToPwrOff) && (millis() - btnCPressTime > 800))
      {
        longPress = 1;
        sprintf(tmpstr, "OFF in %1d   ", timeToPwrOff);
        M5.Lcd.drawString(tmpstr, 210, 220, GFXFF);
        lastDispTime = timeToPwrOff;
      }
      if (timeToPwrOff <= 0)
      {
        // play_tone(3000, 100, 1);
        M5.Power.setWakeupButton(BUTTON_C_PIN);
        M5.Power.powerOFF();
      }
      M5.update();
    }
    if (longPress)
    {
      M5.Lcd.fillRect(210, 220, 110, 20, TFT_BLACK);
      drawIcon(246, 220, (uint8_t *)door_icon16x16, TFT_LIGHTGREY);
    }
    else
    {
      dispPage++;
      if (dispPage > maxPage)
        dispPage = 0;
      setPageIconPos(dispPage);
      M5.Lcd.clear(BLACK);
      // msCount = millis()-16000;
      draw_page();
      // play_tone(440, 100, 1);
    }
    /*
    for (int i=0;i<25000;i++) {
      music_data[i]=alarmSndData[i];
    }
    play_music_data(25000, 10); */
  }
}

void drawLogWarningIcon()
{
  if (err_log_ptr > 5)
    drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t *)warning_icon16x16, TFT_YELLOW);
  else if (err_log_ptr > 0)
    drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t *)warning_icon16x16, TFT_LIGHTGREY);
  else
    M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);
}

void drawBatteryStatus(int16_t x, int16_t y)
{
  int8_t battLevel = getBatteryLevel();
  // Serial.print("Battery level: "); Serial.println(battLevel);
  M5.Lcd.fillRect(x, y, 16, 17, TFT_BLACK);
  if (battLevel != -1)
  {
    switch (battLevel)
    {
    case 0:
      drawIcon(x, y + 1, (uint8_t *)bat0_icon16x16, TFT_RED);
      break;
    case 25:
      drawIcon(x, y + 1, (uint8_t *)bat1_icon16x16, TFT_YELLOW);
      break;
    case 50:
      drawIcon(x, y + 1, (uint8_t *)bat2_icon16x16, TFT_WHITE);
      break;
    case 75:
      drawIcon(x, y + 1, (uint8_t *)bat3_icon16x16, TFT_LIGHTGREY);
      break;
    case 100:
      drawIcon(x, y + 0, (uint8_t *)plug_icon16x16, TFT_LIGHTGREY);
      break;
    }
  }
}

void drawSegment(int x, int y, int r1, int r2, float a, int col)
{
  a = (a / 57.2958) - 1.57;
  float a1 = a - 1.57,
        a2 = a + 1.57,
        x1 = x + (cos(a1) * r1),
        y1 = y + (sin(a1) * r1),
        x2 = x + (cos(a2) * r1),
        y2 = y + (sin(a2) * r1),
        x3 = x + (cos(a) * r2),
        y3 = y + (sin(a) * r2);

  M5.Lcd.fillTriangle(x1, y1, x2, y2, x3, y3, col);
}

void setPageIconPos(int page)
{
  switch (page)
  {
  case 0:
    icon_xpos[0] = 144;
    icon_xpos[1] = 144 + 18;
    icon_xpos[2] = 144 + 2 * 18;
    icon_ypos[0] = 0;
    icon_ypos[1] = 0;
    icon_ypos[2] = 0;
    break;
  case 1:
    icon_xpos[0] = 126;
    icon_xpos[1] = 126 + 18;
    icon_xpos[2] = 126 + 18; // 320-16;
    icon_ypos[0] = 0;
    icon_ypos[1] = 0;
    icon_ypos[2] = 18; // 220;
    break;
  case 2:
    icon_xpos[0] = 0 + 18;
    icon_xpos[1] = 0 + 18;
    icon_xpos[2] = 0 + 18;
    icon_ypos[0] = 110 - 18 - 9;
    icon_ypos[1] = 110 - 18 + 9;
    icon_ypos[2] = 110 - 18 + 27;
    break;
  default:
    icon_xpos[0] = 266;
    icon_xpos[1] = 266 + 18;
    icon_xpos[2] = 266 + 2 * 18;
    icon_ypos[0] = 0;
    icon_ypos[1] = 0;
    icon_ypos[2] = 0;
    break;
  }
}

void drawArrow(int x, int y, int asize, int aangle, int pwidth, int plength, uint16_t color)
{
  float dx = (asize - 10) * cos(aangle - 90) * PI / 180 + x; // calculate X position
  float dy = (asize - 10) * sin(aangle - 90) * PI / 180 + y; // calculate Y position
  float x1 = 0;
  float y1 = plength;
  float x2 = pwidth / 2;
  float y2 = pwidth / 2;
  float x3 = -pwidth / 2;
  float y3 = pwidth / 2;
  float angle = aangle * PI / 180 - 135;
  float xx1 = x1 * cos(angle) - y1 * sin(angle) + dx;
  float yy1 = y1 * cos(angle) + x1 * sin(angle) + dy;
  float xx2 = x2 * cos(angle) - y2 * sin(angle) + dx;
  float yy2 = y2 * cos(angle) + x2 * sin(angle) + dy;
  float xx3 = x3 * cos(angle) - y3 * sin(angle) + dx;
  float yy3 = y3 * cos(angle) + x3 * sin(angle) + dy;
  M5.Lcd.fillTriangle(xx1, yy1, xx3, yy3, xx2, yy2, color);
  M5.Lcd.drawLine(x, y, xx1, yy1, color);
  M5.Lcd.drawLine(x + 1, y, xx1 + 1, yy1, color);
  M5.Lcd.drawLine(x, y + 1, xx1, yy1 + 1, color);
  M5.Lcd.drawLine(x - 1, y, xx1 - 1, yy1, color);
  M5.Lcd.drawLine(x, y - 1, xx1, yy1 - 1, color);
  M5.Lcd.drawLine(x + 2, y, xx1 + 2, yy1, color);
  M5.Lcd.drawLine(x, y + 2, xx1, yy1 + 2, color);
  M5.Lcd.drawLine(x - 2, y, xx1 - 2, yy1, color);
  M5.Lcd.drawLine(x, y - 2, xx1, yy1 - 2, color);
}

void drawMiniGraph(struct NSinfo *ns)
{
  /*
  // draw help lines
  for(int i=0; i<320; i+=40) {
    M5.Lcd.drawLine(i, 0, i, 240, TFT_DARKGREY);
  }
  for(int i=0; i<240; i+=30) {
    M5.Lcd.drawLine(0, i, 320, i, TFT_DARKGREY);
  }
  M5.Lcd.drawLine(0, 120, 320, 120, TFT_LIGHTGREY);
  M5.Lcd.drawLine(160, 0, 160, 240, TFT_LIGHTGREY);
  */
  int i;
  float glk;
  uint16_t sgvColor;
  // M5.Lcd.drawLine(231, 110, 319, 110, TFT_DARKGREY);
  // M5.Lcd.drawLine(231, 110, 231, 207, TFT_DARKGREY);
  // M5.Lcd.drawLine(231, 207, 319, 207, TFT_DARKGREY);
  // M5.Lcd.drawLine(319, 110, 319, 207, TFT_DARKGREY);
  M5.Lcd.drawLine(231, 113, 319, 113, TFT_LIGHTGREY);
  M5.Lcd.drawLine(231, 203, 319, 203, TFT_LIGHTGREY);
  M5.Lcd.drawLine(231, 200 - (4 - 3) * 10 + 3, 319, 200 - (4 - 3) * 10 + 3, TFT_LIGHTGREY);
  M5.Lcd.drawLine(231, 200 - (9 - 3) * 10 + 3, 319, 200 - (9 - 3) * 10 + 3, TFT_LIGHTGREY);
  Serial.print("Last 10 values: ");
  for (i = 9; i >= 0; i--)
  {
    sgvColor = TFT_GREEN;
    glk = *(ns->last10sgv + 9 - i);
    if (glk > 12)
    {
      glk = 12;
    }
    else
    {
      if (glk < 3)
      {
        glk = 3;
      }
    }
    if (glk < cfg.red_low || glk > cfg.red_high)
    {
      sgvColor = TFT_RED;
    }
    else
    {
      if (glk < cfg.yellow_low || glk > cfg.yellow_high)
      {
        sgvColor = TFT_YELLOW;
      }
    }
    Serial.print(*(ns->last10sgv + i));
    Serial.print(" ");
    if (*(ns->last10sgv + 9 - i) != 0)
      M5.Lcd.fillCircle(234 + i * 9, 203 - (glk - 3.0) * 10.0, 3, sgvColor);
  }
  Serial.println();
}

void copyConfig(tConfig *cfg)
{
  Serial.print("yellow_low = ");
  cfg->yellow_low = config.warningLow;
  if (cfg->show_mgdl)
    cfg->yellow_low /= 18.0;
  Serial.println(cfg->yellow_low);

  Serial.print("yellow_high = ");
  cfg->yellow_high = config.alarmHigh;
  if (cfg->show_mgdl)
    cfg->yellow_high /= 18.0;
  Serial.println(cfg->yellow_high);

  Serial.print("red_low = ");
  cfg->red_low = config.alarmLow;
  if (cfg->show_mgdl)
    cfg->red_low /= 18.0;
  Serial.println(cfg->red_low);

  Serial.print("red_high = ");
  cfg->red_high = config.alarmHigh;
  if (cfg->show_mgdl)
    cfg->red_high /= 18.0;
  Serial.println(cfg->red_high);

  Serial.print("snd_warning = ");
  cfg->snd_warning = config.warningLow;
  if (cfg->show_mgdl)
    cfg->snd_warning /= 18.0;
  Serial.println(cfg->snd_warning);

  Serial.print("snd_alarm = ");
  cfg->snd_alarm = config.alarmLow;
  if (cfg->show_mgdl)
    cfg->snd_alarm /= 18.0;
  Serial.println(cfg->snd_alarm);

  Serial.print("snd_warning_high = ");
  cfg->snd_warning_high = config.warningHigh;
  if (cfg->show_mgdl)
    cfg->snd_warning_high /= 18.0;
  Serial.println(cfg->snd_warning_high);

  Serial.print("snd_alarm_high = ");
  cfg->snd_alarm_high = config.alarmHigh;
  if (cfg->show_mgdl)
    cfg->snd_alarm_high /= 18.0;
  Serial.println(cfg->snd_alarm_high);
}

void readNssettings(char *url, char *token)
{
  HTTPClient http;
  int err = 0;
  char NSurlSettings[128];

  if ((WiFi.status() == WL_CONNECTED))
  {
    // configure target server and url
    if (strncmp(url, "http", 4))
    {
      strcpy(NSurlSettings, "https://");
    }
    else
    {
      strcpy(NSurlSettings, "");
    }
    strcat(NSurlSettings, url);
    strcat(NSurlSettings, "/api/v1/profile/current");
    Serial.print("NSurlSettings:");
    Serial.println(NSurlSettings);

    if ((token != NULL) && (strlen(token) > 0))
    {
      if (strchr(NSurlSettings, '?'))
      {
        strcat(NSurlSettings, "&token=");
      }
      else
      {
        strcat(NSurlSettings, "?token=");
      }
      strcat(NSurlSettings, token);
      Serial.print("NSurlSettings:");
      Serial.println(NSurlSettings);
    }
  }

  http.begin(NSurlSettings); //HTTP

  Serial.print("[HTTP] GET...\n");
  // start connection and send HTTP header
  int httpCode = http.GET();

  // httpCode will be negative on error
  if (httpCode > 0)
  {
    // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK)
    {
      String json = http.getString();
      // remove any non text characters (just for sure)
      for (int i = 0; i < json.length(); i++)
      {
        // Serial.print(json.charAt(i), DEC); Serial.print(" = "); Serial.println(json.charAt(i));
        if (json.charAt(i) < 32 /* || json.charAt(i)=='\\' */)
        {
          json.setCharAt(i, 32);
        }
      }
      // json.replace("\\n"," ");
      // invalid Unicode character defined by Ascensia Diabetes Care Bluetooth Glucose Meter
      // ArduinoJSON does not accept any unicode surrogate pairs like \u0032 or \u0000
      json.replace("\\u0000", " ");
      json.replace("\\u0032", " ");
      Serial.print("Free Heap = ");
      Serial.println(ESP.getFreeHeap());
      DeserializationError JSONerr = deserializeJson(JSONdoc, json);
      Serial.println("JSON deserialized OK");
      JsonArray arr = JSONdoc.as<JsonArray>();
      Serial.print("JSON array size = ");
      Serial.println(arr.size());
      if (JSONerr)
      { //Check for errors in parsing
        Serial.print("解析出错了鹅鹅鹅饿");
        if (JSONerr)
        {
          err = 1001; // "JSON parsing failed"
        }
        else
        {
          err = 1002; // "No data from Nightscout"
        }
        addErrorLog(err);
      }
      else
      {
        JsonObject profileObj;
        profileObj = JSONdoc.as<JsonObject>();
        String bgUnit = profileObj["units"];
        Serial.print("units:");
        Serial.println(bgUnit);

        if (bgUnit == "mg/dl")
        {
          cfg.show_mgdl = 1;
        }
        else
        {
          cfg.show_mgdl = 0;
        }
      }
    }
  }
}

int readNightscout(char *url, char *token, struct NSinfo *ns)
{
  HTTPClient http;
  char NSurl[128];
  int err = 0;
  char tmpstr[32];

  if ((WiFi.status() == WL_CONNECTED))
  {
    // configure target server and url
    if (strncmp(url, "http", 4))
    {
      strcpy(NSurl, "https://");
    }
    else
    {
      strcpy(NSurl, "");
    }
    strcat(NSurl, url);

    if ((token != NULL) && (strlen(token) > 0))
    {
      if (strchr(NSurl, '?'))
      {
        strcat(NSurl, "&token=");
      }
      else
      {
        strcat(NSurl, "?token=");
      }
      strcat(NSurl, token);
    }

    if (strstr(NSurl, "sugarmate") != NULL) // Sugarmate JSON URL for Dexcom follower
      ns->is_Sugarmate = 1;
    else
    {
      ns->is_Sugarmate = 0;
      if (cfg.sgv_only)
      {
        strcat(NSurl, "/api/v1/entries.json?find[type][$eq]=sgv");
      }
      else
      {
        strcat(NSurl, "/api/v1/entries.json");
      }
      if ((token != NULL) && (strlen(token) > 0))
      {
        if (strchr(NSurl, '?'))
          strcat(NSurl, "&token=");
        else
          strcat(NSurl, "?token=");
        strcat(NSurl, token);
      }
    }

    M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);
    drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t *)wifi2_icon16x16, TFT_BLUE);

    Serial.print("JSON query NSurl = \'");
    Serial.print(NSurl);
    Serial.print("\'\n");
    http.begin(NSurl); //HTTP

    Serial.print("[HTTP] GET...\n");
    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0)
    {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK)
      {
        String json = http.getString();
        // remove any non text characters (just for sure)
        for (int i = 0; i < json.length(); i++)
        {
          // Serial.print(json.charAt(i), DEC); Serial.print(" = "); Serial.println(json.charAt(i));
          if (json.charAt(i) < 32 /* || json.charAt(i)=='\\' */)
          {
            json.setCharAt(i, 32);
          }
        }
        // json.replace("\\n"," ");
        // invalid Unicode character defined by Ascensia Diabetes Care Bluetooth Glucose Meter
        // ArduinoJSON does not accept any unicode surrogate pairs like \u0032 or \u0000
        json.replace("\\u0000", " ");
        json.replace("\\u0032", " ");
        // Serial.println(json);
        // const size_t capacity = JSON_ARRAY_SIZE(10) + 10*JSON_OBJECT_SIZE(19) + 3840;
        // Serial.print("JSON size needed= "); Serial.print(capacity);
        Serial.print("Free Heap = ");
        Serial.println(ESP.getFreeHeap());
        DeserializationError JSONerr = deserializeJson(JSONdoc, json);
        Serial.println("JSON deserialized OK");
        JsonArray arr = JSONdoc.as<JsonArray>();
        Serial.print("JSON array size = ");
        Serial.println(arr.size());
        if (JSONerr || (ns->is_Sugarmate == 0 && arr.size() == 0))
        { //Check for errors in parsing
          if (JSONerr)
          {
            err = 1001; // "JSON parsing failed"
          }
          else
          {
            err = 1002; // "No data from Nightscout"
          }
          addErrorLog(err);
        }
        else
        {
          JsonObject obj;
          if (ns->is_Sugarmate == 0)
          {
            // Nightscout values

            int sgvindex = 0;
            do
            {
              obj = JSONdoc[sgvindex].as<JsonObject>();
              sgvindex++;
            } while ((!obj.containsKey("sgv")) && (sgvindex < (arr.size() - 1)));
            sgvindex--;
            if (sgvindex < 0 || sgvindex > (arr.size() - 1))
              sgvindex = 0;
            strlcpy(ns->sensDev, JSONdoc[sgvindex]["device"] | "N/A", 64);
            ns->is_xDrip = obj.containsKey("xDrip_raw");
            ns->rawtime = JSONdoc[sgvindex]["date"].as<long long>(); // sensTime is time in milliseconds since 1970, something like 1555229938118
            ns->sensTime = ns->rawtime / 1000;                       // no milliseconds, since 2000 would be - 946684800, but ok
            strlcpy(ns->sensDir, JSONdoc[sgvindex]["direction"] | "N/A", 32);
            ns->sensSgv = JSONdoc[sgvindex]["sgv"]; // get value of sensor measurement
            for (int i = 0; i <= 9; i++)
            {
              ns->last10sgv[i] = JSONdoc[i]["sgv"];
              ns->last10sgv[i] /= 18.0;
            }
          }
          else
          {
            // Sugarmate values
            strcpy(ns->sensDev, "Sugarmate");
            ns->is_xDrip = 0;
            ns->sensSgv = JSONdoc["value"]; // get value of sensor measurement
            time_t tmptime = JSONdoc["x"];  // time in milliseconds since 1970
            if (ns->sensTime != tmptime)
            {
              for (int i = 9; i > 0; i--)
              { // add new value and shift buffer
                ns->last10sgv[i] = ns->last10sgv[i - 1];
              }
              ns->last10sgv[0] = ns->sensSgv;
              ns->sensTime = tmptime;
            }
            ns->rawtime = (long long)ns->sensTime * (long long)1000; // possibly not needed, but to make the structure values complete
            strlcpy(ns->sensDir, JSONdoc["trend_words"] | "N/A", 32);
            ns->delta_mgdl = JSONdoc["delta"]; // get value of sensor measurement
            ns->delta_absolute = ns->delta_mgdl;
            ns->delta_interpolated = 0;
            ns->delta_scaled = ns->delta_mgdl / 18.0;
            if (cfg.show_mgdl)
            {
              sprintf(ns->delta_display, "%+d", ns->delta_mgdl);
            }
            else
            {
              sprintf(ns->delta_display, "%+.1f", ns->delta_scaled);
            }
          }
          ns->sensSgvMgDl = ns->sensSgv;
          // internally we work in mmol/L
          ns->sensSgv /= 18.0;

          localtime_r(&ns->sensTime, &ns->sensTm);

          ns->arrowAngle = 180;
          if (strcmp(ns->sensDir, "DoubleDown") == 0 || strcmp(ns->sensDir, "DOUBLE_DOWN") == 0)
            ns->arrowAngle = 90;
          else if (strcmp(ns->sensDir, "SingleDown") == 0 || strcmp(ns->sensDir, "SINGLE_DOWN") == 0)
            ns->arrowAngle = 75;
          else if (strcmp(ns->sensDir, "FortyFiveDown") == 0 || strcmp(ns->sensDir, "FORTY_FIVE_DOWN") == 0)
            ns->arrowAngle = 45;
          else if (strcmp(ns->sensDir, "Flat") == 0 || strcmp(ns->sensDir, "FLAT") == 0)
            ns->arrowAngle = 0;
          else if (strcmp(ns->sensDir, "FortyFiveUp") == 0 || strcmp(ns->sensDir, "FORTY_FIVE_UP") == 0)
            ns->arrowAngle = -45;
          else if (strcmp(ns->sensDir, "SingleUp") == 0 || strcmp(ns->sensDir, "SINGLE_UP") == 0)
            ns->arrowAngle = -75;
          else if (strcmp(ns->sensDir, "DoubleUp") == 0 || strcmp(ns->sensDir, "DOUBLE_UP") == 0)
            ns->arrowAngle = -90;
          else if (strcmp(ns->sensDir, "NONE") == 0)
            ns->arrowAngle = 180;
          else if (strcmp(ns->sensDir, "NOT COMPUTABLE") == 0)
            ns->arrowAngle = 180;

          Serial.print("sensDev = ");
          Serial.println(ns->sensDev);
          Serial.print("sensTime = ");
          Serial.print(ns->sensTime);
          sprintf(tmpstr, " (JSON %lld)", (long long)ns->rawtime);
          Serial.print(tmpstr);
          sprintf(tmpstr, " = %s", ctime(&ns->sensTime));
          Serial.print(tmpstr);
          Serial.print("sensSgv = ");
          Serial.println(ns->sensSgv);
          Serial.print("sensDir = ");
          Serial.println(ns->sensDir);
          // Serial.print(ns->sensTm.tm_year+1900); Serial.print(" / "); Serial.print(ns->sensTm.tm_mon+1); Serial.print(" / "); Serial.println(ns->sensTm.tm_mday);
          Serial.print("Sensor time: ");
          Serial.print(ns->sensTm.tm_hour);
          Serial.print(":");
          Serial.print(ns->sensTm.tm_min);
          Serial.print(":");
          Serial.print(ns->sensTm.tm_sec);
          Serial.print(" DST ");
          Serial.println(ns->sensTm.tm_isdst);
        }
      }
      else
      {
        addErrorLog(httpCode);
        err = httpCode;
      }
    }
    else
    {
      addErrorLog(httpCode);
      err = httpCode;
    }
    http.end();

    if (err != 0)
    {
      Serial.printf("Returnining with error %d\n", err);
      return err;
    }

    if (ns->is_Sugarmate)
      return 0; // no second query if using Sugarmate

    // the second query
    if (strncmp(url, "http", 4))
      strcpy(NSurl, "https://");
    else
      strcpy(NSurl, "");
    strcat(NSurl, url);
    strcat(NSurl, "/api/v2/properties/iob,cob,delta,loop,basal");
    if (strlen(token) > 0)
    {
      strcat(NSurl, "&token=");
      strcat(NSurl, token);
    }

    M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);
    drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t *)wifi1_icon16x16, TFT_BLUE);

    Serial.print("Properties query NSurl = \'");
    Serial.print(NSurl);
    Serial.print("\'\n");
    http.begin(NSurl); //HTTP
    Serial.print("[HTTP] GET properties...\n");
    httpCode = http.GET();
    if (httpCode > 0)
    {
      Serial.printf("[HTTP] GET properties... code: %d\n", httpCode);
      if (httpCode == HTTP_CODE_OK)
      {
        // const char* propjson = "{\"iob\":{\"iob\":0,\"activity\":0,\"source\":\"OpenAPS\",\"device\":\"openaps://Spike iPhone 8 Plus\",\"mills\":1557613521000,\"display\":\"0\",\"displayLine\":\"IOB: 0U\"},\"cob\":{\"cob\":0,\"source\":\"OpenAPS\",\"device\":\"openaps://Spike iPhone 8 Plus\",\"mills\":1557613521000,\"treatmentCOB\":{\"decayedBy\":\"2019-05-11T23:05:00.000Z\",\"isDecaying\":0,\"carbs_hr\":20,\"rawCarbImpact\":0,\"cob\":7,\"lastCarbs\":{\"_id\":\"5cd74c26156712edb4b32455\",\"enteredBy\":\"Martin\",\"eventType\":\"Carb Correction\",\"reason\":\"\",\"carbs\":7,\"duration\":0,\"created_at\":\"2019-05-11T22:24:00.000Z\",\"mills\":1557613440000,\"mgdl\":67}},\"display\":0,\"displayLine\":\"COB: 0g\"},\"delta\":{\"absolute\":-4,\"elapsedMins\":4.999483333333333,\"interpolated\":false,\"mean5MinsAgo\":69,\"mgdl\":-4,\"scaled\":-0.2,\"display\":\"-0.2\",\"previous\":{\"mean\":69,\"last\":69,\"mills\":1557613221946,\"sgvs\":[{\"mgdl\":69,\"mills\":1557613221946,\"device\":\"MIAOMIAO\",\"direction\":\"Flat\",\"filtered\":92588,\"unfiltered\":92588,\"noise\":1,\"rssi\":100}]}}}";
        String propjson = http.getString();
        // remove any non text characters (just for sure)
        for (int i = 0; i < propjson.length(); i++)
        {
          // Serial.print(propjson.charAt(i), DEC); Serial.print(" = "); Serial.println(propjson.charAt(i));
          if (propjson.charAt(i) < 32 /* || propjson.charAt(i)=='\\' */)
          {
            propjson.setCharAt(i, 32);
          }
        }
        // propjson.replace("\\n"," ");
        // invalid Unicode character defined by Ascensia Diabetes Care Bluetooth Glucose Meter
        // ArduinoJSON does not accept any unicode surrogate pairs like \u0032 or \u0000
        propjson.replace("\\u0000", " ");
        propjson.replace("\\u0032", " ");
        DeserializationError propJSONerr = deserializeJson(JSONdoc, propjson);
        if (propJSONerr)
        {
          err = 1003; // "JSON2 parsing failed"
          addErrorLog(err);
        }
        else
        {
          Serial.println("Deserialized the second JSON and OK");
          JsonObject iob = JSONdoc["iob"];
          ns->iob = iob["iob"];                                              // 0
          strncpy(ns->iob_display, iob["display"] | "N/A", 16);              // 0
          strncpy(ns->iob_displayLine, iob["displayLine"] | "IOB: N/A", 16); // "IOB: 0U"
          // Serial.println("IOB OK");

          JsonObject cob = JSONdoc["cob"];
          ns->cob = cob["cob"];                                              // 0
          strncpy(ns->cob_display, cob["display"] | "N/A", 16);              // 0
          strncpy(ns->cob_displayLine, cob["displayLine"] | "COB: N/A", 16); // "COB: 0g"
          // Serial.println("COB OK");

          JsonObject delta = JSONdoc["delta"];
          ns->delta_absolute = delta["absolute"];                // -4
          ns->delta_elapsedMins = delta["elapsedMins"];          // 4.999483333333333
          ns->delta_interpolated = delta["interpolated"];        // false
          ns->delta_mean5MinsAgo = delta["mean5MinsAgo"];        // 69
          ns->delta_mgdl = delta["mgdl"];                        // -4
          ns->delta_scaled = delta["scaled"];                    // -0.2
          strncpy(ns->delta_display, delta["display"] | "", 16); // "-0.2"
          // Serial.println("DELTA OK");

          JsonObject loop_obj = JSONdoc["loop"];
          JsonObject loop_display = loop_obj["display"];
          strncpy(tmpstr, loop_display["symbol"] | "?", 4); // "⌁"
          ns->loop_display_symbol = tmpstr[0];
          strncpy(ns->loop_display_code, loop_display["code"] | "N/A", 16);   // "enacted"
          strncpy(ns->loop_display_label, loop_display["label"] | "N/A", 16); // "Enacted"
          // Serial.println("LOOP OK");

          JsonObject basal = JSONdoc["basal"];
          strncpy(ns->basal_display, basal["display"] | "N/A", 16); // "T: 0.950U"
          // Serial.println("BASAL OK");

          JsonObject basal_current_doc = JSONdoc["basal"]["current"];
          ns->basal_current = basal_current_doc["basal"];                   // 0.1
          ns->basal_tempbasal = basal_current_doc["tempbasal"];             // 0.1
          ns->basal_combobolusbasal = basal_current_doc["combobolusbasal"]; // 0
          ns->basal_totalbasal = basal_current_doc["totalbasal"];           // 0.1
          // Serial.println("LOOP OK");
        }
      }
      else
      {
        addErrorLog(httpCode);
        err = httpCode;
      }
    }
    else
    {
      addErrorLog(httpCode);
      err = httpCode;
    }
    http.end();
  }
  else
  {
    // WiFi not connected
    ESP.restart();
  }

  M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);

  return err;
}
////////////////////////////////////////////////////////////////

size_t trimwhitespace(char *out, size_t len, const char *str)
{
  if (len == 0)
    return 0;

  const char *end;
  size_t out_size;

  // Trim leading space
  while (isspace((unsigned char)*str))
    str++;

  if (*str == 0) // All spaces?
  {
    *out = 0;
    return 1;
  }

  // Trim trailing space
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end))
    end--;
  end++;

  // Set output size to minimum of trimmed string length and buffer size minus 1
  out_size = (end - str) < len - 1 ? (end - str) : len - 1;

  // Copy trimmed string and add null terminator
  // char*ptr = malloc(32);
  if (!out)
  {
    perror("malloc");
    exit(EXIT_FAILURE);
  };
  memcpy(out, str, out_size);
  out[out_size] = 0;

  return out_size;
}

int readTomatoRemote(const char *shareID, struct NSinfo *ns)
{
  HTTPClient http;
  char NSurl[256] = "http://testapi.tomato.cool/m5stack/glycemic/";
  int err = 0;
  char tmpstr[32];
  char deviceid[16];
  strcpy(deviceid, WiFi.macAddress().c_str());

  // unsigned long endTime = esp_timer_get_time() * 1000000;
  size_t shareIdLen = strlen(shareID) + 1;
  char tomatoshareIDinURL[shareIdLen];

  trimwhitespace(tomatoshareIDinURL, shareIdLen, shareID);

  /**
   * TODO: modify it to real value
   *    
  */
  const char *endTime = "1592956800000";
  Serial.print("endTime:");
  Serial.println(endTime);
  const char *startTime = "1592870400000";
  Serial.print("startTime:");
  Serial.println(startTime);

  Serial.print("deviceid:");
  Serial.println(F(deviceid));
  strcat(NSurl, tomatoshareIDinURL);
  strcat(NSurl, "?device_id=");
  strcat(NSurl, deviceid);

  /**
   * Only for test
  */
  strcat(NSurl, "&start_time=");
  strcat(NSurl, String(startTime).c_str());
  strcat(NSurl, "&end_time=");
  strcat(NSurl, String(endTime).c_str());

  if ((WiFi.status() == WL_CONNECTED))
  {
    // configure target server and url

    M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);
    drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t *)wifi2_icon16x16, TFT_BLUE);

    Serial.print("JSON query NSurl = \'");
    Serial.print(NSurl);
    Serial.print("\'\n");
    http.begin(NSurl); //HTTP

    Serial.print("[HTTP] GET...\n");
    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0)
    {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK)
      {
        String json = http.getString();
        Serial.print(json);
        // remove any non text characters (just for sure)
        for (int i = 0; i < json.length(); i++)
        {
          // Serial.print(json.charAt(i), DEC); Serial.print(" = "); Serial.println(json.charAt(i));
          if (json.charAt(i) < 32 /* || json.charAt(i)=='\\' */)
          {
            json.setCharAt(i, 32);
          }
        }
        // json.replace("\\n"," ");
        // invalid Unicode character defined by Ascensia Diabetes Care Bluetooth Glucose Meter
        // ArduinoJSON does not accept any unicode surrogate pairs like \u0032 or \u0000
        json.replace("\\u0000", " ");
        json.replace("\\u0032", " ");
        // Serial.println(json);
        // const size_t capacity = JSON_ARRAY_SIZE(10) + 10*JSON_OBJECT_SIZE(19) + 3840;
        // Serial.print("JSON size needed= "); Serial.print(capacity);
        Serial.print("Free Heap = ");
        Serial.println(ESP.getFreeHeap());
        DeserializationError JSONerr = deserializeJson(JSONdoc, json);
        Serial.println("JSON deserialized OK");
        JsonArray arr = JSONdoc["data"]["glycemic_list"].as<JsonArray>();
        Serial.print("JSON array size = ");
        Serial.println(arr.size());
        if (JSONerr || (arr.size() == 0))
        { //Check for errors in parsing
          if (JSONerr)
          {
            err = 1001; // "JSON parsing failed"
          }
          else
          {
            err = 1002; // "No data from Nightscout"
          }
          addErrorLog(err);
        }
        else
        {
          JsonObject obj;

          // Nightscout values

          int sgvindex = 0;
          do
          {
            obj = JSONdoc["data"].as<JsonObject>();
            sgvindex++;
          } while ((!obj.containsKey("glycemic")) && (sgvindex < (arr.size() - 1)));
          sgvindex--;
          if (sgvindex < 0 || sgvindex > (arr.size() - 1))
            sgvindex = 0;
          strlcpy(ns->sensDev, "Tomato", 64);
          ns->is_xDrip = obj.containsKey("xDrip_raw");
          ns->rawtime = JSONdoc["data"]["glycemic_list"][sgvindex]["time"].as<long long>(); // sensTime is time in milliseconds since 1970, something like 1555229938118
          ns->sensTime = ns->rawtime / 1000;                                                // no milliseconds, since 2000 would be - 946684800, but ok
          int arrowNum = JSONdoc["data"]["arrow"];
          String arrowStr = String(arrowNum);
          strlcpy(ns->sensDir, arrowStr.c_str(), 32);
          float change = JSONdoc["data"]["change"];
          int bgUnit = JSONdoc["data"]["bgUnit"];
          if (bgUnit == 1)
          {
            cfg.show_mgdl = 0;
          }
          else if (bgUnit == 2)
          {
            cfg.show_mgdl = 1;
          };

          int range = JSONdoc["data"]["range"];
          cfg.range = range;
          float changeMgdl = change * 18.0032;
          Serial.print("change:");
          Serial.println(change);
          Serial.print("changeMgdl:");
          Serial.println(changeMgdl);

          ns->delta_mgdl = changeMgdl; // get value of sensor measurement
          ns->delta_scaled = change;

          Serial.print("delta_mgdl:");
          Serial.println(ns->delta_mgdl);
          Serial.print("delta_scaled:");
          Serial.println(ns->delta_scaled);
          if (cfg.show_mgdl)
          {
            sprintf(ns->delta_display, "%+d", ns->delta_mgdl);
          }
          else
          {
            sprintf(ns->delta_display, "%+.1f", ns->delta_scaled);
          }

          Serial.print("ns->delta_display");
          Serial.print(ns->delta_display);
          ns->sensSgv = JSONdoc["data"]["glycemic"]; // get value of sensor measurement
          for (int i = 0; i < 10; i++)
          {
            ns->last10sgv[9 - i] = JSONdoc["data"]["glycemic_list"][i]["glycemic"];
            // ns->last10sgv[i] /= 18.0;
          }
        }

        Serial.print("ns->delta_display");
        Serial.print(ns->delta_display);
        ns->sensSgvMgDl = ns->sensSgv * 18.032;
        // internally we work in mmol/L
        // ns->sensSgv /= 18.0;

        localtime_r(&ns->sensTime, &ns->sensTm);

        ns->arrowAngle = 180;
        if (strcmp(ns->sensDir, "1") == 0)
          ns->arrowAngle = 90;
        else if (strcmp(ns->sensDir, "2") == 0)
          ns->arrowAngle = 45;
        else if ((strcmp(ns->sensDir, "3") == 0) || (strcmp(ns->sensDir, "0") == 0))
          ns->arrowAngle = 0;
        else if (strcmp(ns->sensDir, "4") == 0)
          ns->arrowAngle = -45;
        else if (strcmp(ns->sensDir, "5") == 0)
          ns->arrowAngle = -90;

        Serial.print("sensDev = ");
        Serial.println(ns->sensDev);
        Serial.print("sensTime = ");
        Serial.print(ns->sensTime);
        sprintf(tmpstr, " (JSON %lld)", (long long)ns->rawtime);
        Serial.print(tmpstr);
        sprintf(tmpstr, " = %s", ctime(&ns->sensTime));
        Serial.print(tmpstr);
        Serial.print("sensSgv = ");
        Serial.println(ns->sensSgv);
        Serial.print("sensDir = ");
        Serial.println(ns->sensDir);
        // Serial.print(ns->sensTm.tm_year+1900); Serial.print(" / "); Serial.print(ns->sensTm.tm_mon+1); Serial.print(" / "); Serial.println(ns->sensTm.tm_mday);
        Serial.print("Sensor time: ");
        Serial.print(ns->sensTm.tm_hour);
        Serial.print(":");
        Serial.print(ns->sensTm.tm_min);
        Serial.print(":");
        Serial.print(ns->sensTm.tm_sec);
        Serial.print(" DST ");
        Serial.println(ns->sensTm.tm_isdst);
      }
      else
      {
        addErrorLog(httpCode);
        err = httpCode;
      }
    }
    else
    {
      addErrorLog(httpCode);
      err = httpCode;
    }
    http.end();

    if (err != 0)
    {
      Serial.printf("Returnining with error %d\n", err);
      return err;
    }
  }

  return err;
}

void draw_page()
{
  char tmpstr[255];

  M5.Lcd.setTextDatum(TL_DATUM);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(0, 0);

  switch (dispPage)
  {
  case 0:
  {
    // if there was an error, then clear whole screen, otherwise only graphic updated part
    // M5.Lcd.fillScreen(BLACK);
    // M5.Lcd.fillRect(230, 110, 90, 100, TFT_BLACK);
    if (config.dataSource != 2)
    {
      readTomatoRemote(config.tomatoShareID, &ns);
    }
    else
    {
      readNssettings(cfg.url, cfg.token);
      readNightscout(cfg.url, cfg.token, &ns);
    };

    M5.Lcd.setFreeFont(FSSB12);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    // char dateStr[30];
    // sprintf(dateStr, "%d.%d.%04d", sensTm.tm_mday, sensTm.tm_mon+1, sensTm.tm_year+1900);
    // M5.Lcd.drawString(dateStr, 0, 48, GFXFF);
    // char timeStr[30];
    // sprintf(timeStr, "%02d:%02d:%02d", sensTm.tm_hour, sensTm.tm_min, sensTm.tm_sec);
    // M5.Lcd.drawString(timeStr, 0, 72, GFXFF);
    char datetimeStr[30];
    struct tm timeinfo;
    if (cfg.show_current_time)
    {
      if (getLocalTime(&timeinfo))
      {
        // sprintf(datetimeStr, "%02d:%02d:%02d  %d.%d.  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon+1);
        // timeinfo.tm_mday=24; timeinfo.tm_mon=11;
        switch (cfg.date_format)
        {
        case 1:
          sprintf(datetimeStr, "%02d:%02d  %d/%d  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_mon + 1, timeinfo.tm_mday);
          break;
        default:
          sprintf(datetimeStr, "%02d:%02d  %d.%d.  ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_mday, timeinfo.tm_mon + 1);
        }
      }
      else
      {
        // strcpy(datetimeStr, "??:??:??");
        strcpy(datetimeStr, "??:??");
      }
    }
    else
    {
      // sprintf(datetimeStr, "%02d:%02d:%02d  %d.%d.  ", sensTm.tm_hour, sensTm.tm_min, sensTm.tm_sec, sensTm.tm_mday, sensTm.tm_mon+1);
      sprintf(datetimeStr, "%02d:%02d  %d.%d.  ", ns.sensTm.tm_hour, ns.sensTm.tm_min, ns.sensTm.tm_mday, ns.sensTm.tm_mon + 1);
    }
    M5.Lcd.drawString(datetimeStr, 0, 0, GFXFF);

    drawBatteryStatus(icon_xpos[2], icon_ypos[2]);

    if (err_log_ptr > 0)
    {
      M5.Lcd.fillRect(icon_xpos[0], icon_ypos[0], 16, 16, BLACK);
      if (err_log_ptr > 5)
        drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t *)warning_icon16x16, TFT_YELLOW);
      else
        drawIcon(icon_xpos[0], icon_ypos[0], (uint8_t *)warning_icon16x16, TFT_LIGHTGREY);
    }

    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.drawString(cfg.userName, 0, 24, GFXFF);

    if (cfg.show_COB_IOB)
    {
      M5.Lcd.setFreeFont(FSSB12);
      M5.Lcd.setTextSize(1);
      // show small delta right from name
      /*
        M5.Lcd.setFreeFont(FSSB12);
        M5.Lcd.setTextColor(WHITE, BLACK);
        M5.Lcd.setTextSize(1);
        M5.Lcd.fillRect(130,24,69,23,TFT_BLACK);
        M5.Lcd.drawString(ns.delta_display, 130, 24, GFXFF);
        */

      M5.Lcd.fillRect(0, 48, 199, 47, TFT_BLACK);
      if (ns.iob > 0)
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      else
        M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      // Serial.print("ns.iob_displayLine=\""); Serial.print(ns.iob_displayLine); Serial.println("\"");
      strncpy(tmpstr, ns.iob_displayLine, 16);
      if (strncmp(tmpstr, "IOB:", 4) == 0)
      {
        strcpy(tmpstr, "I");
        strcat(tmpstr, &ns.iob_displayLine[3]);
      }
      M5.Lcd.drawString(tmpstr, 0, 48, GFXFF);
      if (ns.cob > 0)
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      else
        M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      // Serial.print("ns.cob_displayLine=\""); Serial.print(ns.cob_displayLine); Serial.println("\"");
      strncpy(tmpstr, ns.cob_displayLine, 16);
      if (strncmp(tmpstr, "COB:", 4) == 0)
      {
        strcpy(tmpstr, "C");
        strcat(tmpstr, &ns.cob_displayLine[3]);
      }
      M5.Lcd.drawString(tmpstr, 0, 72, GFXFF);

      // show BIG delta bellow the name
      M5.Lcd.setFreeFont(FSSB24);
      // strcpy(ns.delta_display, "+8.9");
      if (ns.delta_mgdl > 7)
        M5.Lcd.setTextColor(TFT_WHITE, BLACK);
      else
        M5.Lcd.setTextColor(TFT_LIGHTGREY, BLACK);
      M5.Lcd.drawString(ns.delta_display, 103, 48, GFXFF);
      M5.Lcd.setFreeFont(FSSB12);
    }
    else
    {
      // show BIG delta bellow the name
      M5.Lcd.setFreeFont(FSSB24);
      M5.Lcd.setTextColor(TFT_LIGHTGREY, BLACK);
      M5.Lcd.setTextSize(1);
      M5.Lcd.fillRect(0, 48 + 10, 199, 47, TFT_BLACK);
      M5.Lcd.drawString(ns.delta_display, 0, 48 + 10, GFXFF);
      M5.Lcd.setFreeFont(FSSB12);
    }

    // calculate sensor time difference
    int sensorDifSec = 0;
    if (!getLocalTime(&timeinfo))
    {
      sensorDifSec = 24 * 60 * 60; // too much
    }
    else
    {
      Serial.print("Local time: ");
      Serial.print(timeinfo.tm_hour);
      Serial.print(":");
      Serial.print(timeinfo.tm_min);
      Serial.print(":");
      Serial.print(timeinfo.tm_sec);
      Serial.print(" DST ");
      Serial.println(timeinfo.tm_isdst);
      sensorDifSec = difftime(mktime(&timeinfo), ns.sensTime);
    }
    Serial.print("Sensor time difference = ");
    Serial.print(sensorDifSec);
    Serial.println(" sec");
    unsigned int sensorDifMin = (sensorDifSec + 30) / 60;
    uint16_t tdColor = TFT_LIGHTGREY;
    if (sensorDifMin > 5)
    {
      tdColor = TFT_WHITE;
      if (sensorDifMin > 15)
      {
        tdColor = TFT_RED;
      }
    }

    M5.Lcd.fillRoundRect(200, 0, 120, 90, 15, tdColor);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setFreeFont(FSSB24);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setTextColor(TFT_BLACK, tdColor);
    if (sensorDifMin > 99)
    {
      M5.Lcd.drawString("Err", 260, 32, GFXFF);
    }
    else
    {
      M5.Lcd.drawNumber(sensorDifMin, 260, 32, GFXFF);
    }
    M5.Lcd.setTextSize(1);
    M5.Lcd.setFreeFont(FSSB12);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setTextColor(TFT_BLACK, tdColor);
    M5.Lcd.drawString("min", 260, 70, GFXFF);

    uint16_t glColor = TFT_GREEN;
    /**
     * Get the  color of bg
     * - if use tomato, get the color via  the value of range
     * */
    if (config.dataSource != 2)
    {
      if (cfg.range == 0)
      {
        glColor = TFT_GREEN;
      }
      else if ((cfg.range == 1) || (cfg.range == -1))
      {
        glColor = TFT_YELLOW;
      }
      else if ((cfg.range == 2) || (cfg.range == -2))
      {
        glColor = TFT_RED;
      };
    }
    else
    {

      if (ns.sensSgv < cfg.yellow_low || ns.sensSgv > cfg.yellow_high)
      {
        glColor = TFT_YELLOW; // warning is YELLOW
      }
      if (ns.sensSgv < cfg.red_low || ns.sensSgv > cfg.red_high)
      {
        glColor = TFT_RED; // alert is RED
      }
    }

    sprintf(tmpstr, "Glyk: %4.1f %s", ns.sensSgv, ns.sensDir);
    Serial.println(tmpstr);

    M5.Lcd.fillRect(0, 110, 320, 114, TFT_BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.setTextColor(glColor, TFT_BLACK);
    char sensSgvStr[30];
    int smaller_font = 0;

    if (cfg.show_mgdl)
    {
      sprintf(sensSgvStr, "%3.0f", ns.sensSgvMgDl);
    }
    else
    {
      sprintf(sensSgvStr, "%4.1f", ns.sensSgv);
      if (sensSgvStr[0] != ' ')
        smaller_font = 1;
    }
    // Serial.print("SGV string length = "); Serial.print(strlen(sensSgvStr));
    // Serial.print(", smaller_font = "); Serial.println(smaller_font);
    if (smaller_font)
    {
      M5.Lcd.setFreeFont(FSSB18);
      M5.Lcd.drawString(sensSgvStr, 0, 130, GFXFF);
    }
    else
    {
      M5.Lcd.setFreeFont(FSSB24);
      M5.Lcd.drawString(sensSgvStr, 0, 120, GFXFF);
    }
    int tw = M5.Lcd.textWidth(sensSgvStr);
    // int th=M5.Lcd.fontHeight(GFXFF);
    // Serial.print("textWidth="); Serial.println(tw);
    // Serial.print("textHeight="); Serial.println(th);

    if (ns.arrowAngle != 180)
      drawArrow(0 + tw + 25, 120 + 40, 10, ns.arrowAngle + 85, 40, 40, glColor);

    /*
      // draw help lines
      for(int i=0; i<320; i+=40) {
        M5.Lcd.drawLine(i, 0, i, 240, TFT_DARKGREY);
      }
      for(int i=0; i<240; i+=30) {
        M5.Lcd.drawLine(0, i, 320, i, TFT_DARKGREY);
      }
      M5.Lcd.drawLine(0, 120, 320, 120, TFT_LIGHTGREY);
      M5.Lcd.drawLine(160, 0, 160, 240, TFT_LIGHTGREY);
      */

    drawMiniGraph(&ns);
    handleAlarmsInfoLine(&ns);
    drawLogWarningIcon();
  }
  break;

  case 1:
  {
    // readNightscout(cfg.url, cfg.token, &ns);

    uint16_t glColor = TFT_GREEN;
    if (config.dataSource != 2)
    {
      if (cfg.range == 0)
      {
        glColor = TFT_GREEN;
      }
      else if ((cfg.range == 1) || (cfg.range == -1))
      {
        glColor = TFT_YELLOW;
      }
      else if ((cfg.range == 2) || (cfg.range == -2))
      {
        glColor = TFT_RED;
      };
    }
    else
    {

      if (ns.sensSgv < cfg.yellow_low || ns.sensSgv > cfg.yellow_high)
      {
        glColor = TFT_YELLOW; // warning is YELLOW
      }
      if (ns.sensSgv < cfg.red_low || ns.sensSgv > cfg.red_high)
      {
        glColor = TFT_RED; // alert is RED
      }
    }

    sprintf(tmpstr, "Glyk: %4.1f %s", ns.sensSgv, ns.sensDir);
    Serial.println(tmpstr);

    M5.Lcd.fillRect(0, 40, 320, 180, TFT_BLACK);
    M5.Lcd.setTextSize(4);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setTextColor(glColor, TFT_BLACK);
    char sensSgvStr[30];
    // int smaller_font = 0;
    if (cfg.show_mgdl)
    {
      if (ns.sensSgvMgDl < 100)
      {
        sprintf(sensSgvStr, "%2.0f", ns.sensSgvMgDl);
        M5.Lcd.setFreeFont(FSSB24);
      }
      else
      {
        sprintf(sensSgvStr, "%3.0f", ns.sensSgvMgDl);
        M5.Lcd.setFreeFont(FSSB24);
      }
    }
    else
    {
      if (ns.sensSgv < 10)
      {
        sprintf(sensSgvStr, "%3.1f", ns.sensSgv);
        M5.Lcd.setFreeFont(FSSB24);
      }
      else
      {
        sprintf(sensSgvStr, "%4.1f", ns.sensSgv);
        M5.Lcd.setFreeFont(FSSB18);
      }
    }
    M5.Lcd.drawString(sensSgvStr, 160, 120, GFXFF);

    M5.Lcd.fillRect(0, 0, 320, 40, TFT_BLACK);
    M5.Lcd.setFreeFont(FSSB24);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

    char datetimeStr[30];
    struct tm timeinfo;
    if (cfg.show_current_time)
    {
      if (getLocalTime(&timeinfo))
      {
        sprintf(datetimeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      }
      else
      {
        strcpy(datetimeStr, "??:??");
      }
    }
    else
    {
      sprintf(datetimeStr, "%02d:%02d", ns.sensTm.tm_hour, ns.sensTm.tm_min);
    }
    M5.Lcd.drawString(datetimeStr, 0, 0, GFXFF);

    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.drawString(ns.delta_display, 180, 0, GFXFF);

    int ay = 0;

    // for(ns.arrowAngle=-90; ns.arrowAngle<=90; ns.arrowAngle+=15)

    if (ns.arrowAngle >= 45)
      ay = 4;
    else if (ns.arrowAngle > -45)
      ay = 18;
    else
      ay = 30;

    if (ns.arrowAngle != 180)
      drawArrow(280, ay, 10, ns.arrowAngle + 85, 28, 28, glColor);

    handleAlarmsInfoLine(&ns);
    drawBatteryStatus(icon_xpos[2], icon_ypos[2]);
    drawLogWarningIcon();
  }
  break;

  case 2:
  {
    // calculate SGV color
    uint16_t glColor = TFT_GREEN;
    if (config.dataSource != 2)
    {
      if (cfg.range == 0)
      {
        glColor = TFT_GREEN;
      }
      else if ((cfg.range == 1) || (cfg.range == -1))
      {
        glColor = TFT_YELLOW;
      }
      else if ((cfg.range == 2) || (cfg.range == -2))
      {
        glColor = TFT_RED;
      };
    }
    else
    {

      if (ns.sensSgv < cfg.yellow_low || ns.sensSgv > cfg.yellow_high)
      {
        glColor = TFT_YELLOW; // warning is YELLOW
      }
      if (ns.sensSgv < cfg.red_low || ns.sensSgv > cfg.red_high)
      {
        glColor = TFT_RED; // alert is RED
      }
    }

    // display SGV
    sprintf(tmpstr, "Glyk: %4.1f %s", ns.sensSgv, ns.sensDir);
    Serial.println(tmpstr);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.setTextColor(glColor, TFT_BLACK);
    char sensSgvStr[30];
    // int smaller_font = 0;
    if (cfg.show_mgdl)
    {
      if (ns.sensSgvMgDl < 100)
      {
        sprintf(sensSgvStr, "%2.0f", ns.sensSgvMgDl);
        M5.Lcd.setFreeFont(FSSB24);
      }
      else
      {
        sprintf(sensSgvStr, "%3.0f", ns.sensSgvMgDl);
        M5.Lcd.setFreeFont(FSSB24);
      }
    }
    else
    {
      if (ns.sensSgv < 10)
      {
        sprintf(sensSgvStr, "%3.1f", ns.sensSgv);
        M5.Lcd.setFreeFont(FSSB24);
      }
      else
      {
        sprintf(sensSgvStr, "%4.1f", ns.sensSgv);
        M5.Lcd.setFreeFont(FSSB24); //18
      }
    }
    M5.Lcd.fillRect(0, 0, 100, 40, TFT_BLACK);
    M5.Lcd.drawString(sensSgvStr, 0, 0, GFXFF);

    // display DELTA
    M5.Lcd.setTextDatum(TR_DATUM);
    M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Lcd.fillRect(220, 0, 100, 40, TFT_BLACK);
    M5.Lcd.drawString(ns.delta_display, 319, 0, GFXFF);

    // get time - need update
    char datetimeStr[30];
    struct tm timeinfo;
    if (cfg.show_current_time)
    {
      if (getLocalTime(&timeinfo))
      {
        sprintf(datetimeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
      }
      else
      {
        strcpy(datetimeStr, "??:??");
      }
    }
    else
    {
      sprintf(datetimeStr, "%02d:%02d", ns.sensTm.tm_hour, ns.sensTm.tm_min);
    }

    // calculate sensor time difference
    int sensorDifSec = 0;
    if (!getLocalTime(&timeinfo))
    {
      sensorDifSec = 24 * 60 * 60; // too much
    }
    else
    {
      Serial.print("Local time: ");
      Serial.print(timeinfo.tm_hour);
      Serial.print(":");
      Serial.print(timeinfo.tm_min);
      Serial.print(":");
      Serial.print(timeinfo.tm_sec);
      Serial.print(" DST ");
      Serial.println(timeinfo.tm_isdst);
      sensorDifSec = difftime(mktime(&timeinfo), ns.sensTime);
    }
    Serial.print("Sensor time difference = ");
    Serial.print(sensorDifSec);
    Serial.println(" sec");
    unsigned int sensorDifMin = (sensorDifSec + 30) / 60;
    uint16_t tdColor = TFT_LIGHTGREY;
    if (sensorDifMin > 5)
    {
      tdColor = TFT_WHITE;
      if (sensorDifMin > 15)
      {
        tdColor = TFT_RED;
      }
    }
    // display time since last valid data
    M5.Lcd.fillRoundRect(0, 44, 68, 22, 7, tdColor);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setFreeFont(FSS9);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setTextColor(TFT_BLACK, tdColor);
    if (sensorDifMin > 99)
    {
      M5.Lcd.drawString("Err min", 34, 53, GFXFF);
    }
    else
    {
      M5.Lcd.drawString(String(sensorDifMin) + " min", 34, 53, GFXFF);
    }

    // draw temperature
    float tmprc = dht12.readTemperature(cfg.temperature_unit);
    // Serial.print("tmprc="); Serial.println(tmprc);
    if (tmprc != float(0.01) && tmprc != float(0.02) && tmprc != float(0.03))
    { // not an error
      M5.Lcd.setTextDatum(BL_DATUM);
      M5.Lcd.setFreeFont(FSS12); // CF_RT24
      M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      String tmprcStr = String(tmprc, 1);
      int tw = M5.Lcd.textWidth(tmprcStr);
      M5.Lcd.fillRect(0, 180, 88, 30, TFT_BLACK);
      M5.Lcd.drawString(tmprcStr, 7, 210, GFXFF);
      M5.Lcd.setFreeFont(FSS9);
      int ow = M5.Lcd.textWidth("o");
      M5.Lcd.drawString("o", 7 + tw + 2, 199, GFXFF);
      M5.Lcd.setFreeFont(FSS12);
      switch (cfg.temperature_unit)
      {
      case 1:
        M5.Lcd.drawString("C", 7 + tw + ow + 4, 210, GFXFF);
        break;
      case 2:
        M5.Lcd.drawString("K", 7 + tw + ow + 4, 210, GFXFF);
        break;
      case 3:
        M5.Lcd.drawString("F", 7 + tw + ow + 4, 210, GFXFF);
        break;
      }
    }

    // display humidity
    float humid = dht12.readHumidity();
    // Serial.print("humid="); Serial.println(humid);
    if (humid != float(0.01) && humid != float(0.02) && humid != float(0.03))
    { // not an error
      M5.Lcd.setTextDatum(BR_DATUM);
      M5.Lcd.setFreeFont(FSS12);
      M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      String humidStr = String(humid, 0);
      humidStr += "%";
      M5.Lcd.fillRect(250, 185, 70, 25, TFT_BLACK);
      M5.Lcd.drawString(humidStr, 310, 210, GFXFF);
    }

    // draw clock
    float sx = 0, sy = 1, mx = 1, my = 0, hx = -1, hy = 0; // Saved H, M, S x & y multipliers
    float sdeg = 0, mdeg = 0, hdeg = 0;
    uint16_t x0 = 0, x1 = 0, yy0 = 0, yy1 = 0;

    uint8_t hh = timeinfo.tm_hour, mm = timeinfo.tm_min, ss = timeinfo.tm_sec; // Get current time

    // Draw clock face
    M5.Lcd.fillCircle(160, 110, 98, glColor);
    M5.Lcd.fillCircle(160, 110, 92, TFT_BLACK);

    // Draw 12 lines
    for (int i = 0; i < 360; i += 30)
    {
      sx = cos((i - 90) * 0.0174532925);
      sy = sin((i - 90) * 0.0174532925);
      x0 = sx * 94 + 160;
      yy0 = sy * 94 + 110;
      x1 = sx * 80 + 160;
      yy1 = sy * 80 + 110;

      M5.Lcd.drawLine(x0, yy0, x1, yy1, glColor);
    }

    // Draw 60 dots
    for (int i = 0; i < 360; i += 6)
    {
      sx = cos((i - 90) * 0.0174532925);
      sy = sin((i - 90) * 0.0174532925);
      x0 = sx * 82 + 160;
      yy0 = sy * 82 + 110;
      // Draw minute markers
      M5.Lcd.drawPixel(x0, yy0, TFT_WHITE);

      // Draw main quadrant dots
      if (i == 0 || i == 180)
        M5.Lcd.fillCircle(x0, yy0, 2, TFT_WHITE);
      if (i == 90 || i == 270)
        M5.Lcd.fillCircle(x0, yy0, 2, TFT_WHITE);
    }

    M5.Lcd.fillCircle(160, 110, 3, TFT_WHITE);

    // draw day
    M5.Lcd.drawRoundRect(182, 97, 36, 26, 7, TFT_LIGHTGREY);
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Lcd.setFreeFont(FSSB9);
    M5.Lcd.drawString(String(timeinfo.tm_mday), 200, 108, GFXFF);

    // draw name
    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setFreeFont(FSSB9);
    M5.Lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Lcd.drawString(cfg.userName, 160, 145, GFXFF);

    // Pre-compute hand degrees, x & y coords for a fast screen update
    sdeg = ss * 6;                     // 0-59 -> 0-354
    mdeg = mm * 6 + sdeg * 0.01666667; // 0-59 -> 0-360 - includes seconds
    hdeg = hh * 30 + mdeg * 0.0833333; // 0-11 -> 0-360 - includes minutes and seconds
    hx = cos((hdeg - 90) * 0.0174532925);
    hy = sin((hdeg - 90) * 0.0174532925);
    mx = cos((mdeg - 90) * 0.0174532925);
    my = sin((mdeg - 90) * 0.0174532925);
    sx = cos((sdeg - 90) * 0.0174532925);
    sy = sin((sdeg - 90) * 0.0174532925);

    if (ss == 0 || initial)
    {
      initial = 0;
      // Erase hour and minute hand positions every minute
      M5.Lcd.drawLine(ohx, ohy, 160, 110, TFT_BLACK);
      M5.Lcd.drawLine(ohx + 1, ohy, 161, 110, TFT_BLACK);
      M5.Lcd.drawLine(ohx - 1, ohy, 159, 110, TFT_BLACK);
      M5.Lcd.drawLine(ohx, ohy - 1, 160, 109, TFT_BLACK);
      M5.Lcd.drawLine(ohx, ohy + 1, 160, 111, TFT_BLACK);
      ohx = hx * 52 + 160;
      ohy = hy * 52 + 110;
      M5.Lcd.drawLine(omx, omy, 160, 110, TFT_BLACK);
      omx = mx * 74 + 160;
      omy = my * 74 + 110;
    }

    // Redraw new hand positions, hour and minute hands not erased here to avoid flicker
    M5.Lcd.drawLine(osx, osy, 160, 110, TFT_BLACK);
    osx = sx * 78 + 160;
    osy = sy * 78 + 110;
    M5.Lcd.drawLine(ohx, ohy, 160, 110, TFT_WHITE);
    M5.Lcd.drawLine(ohx + 1, ohy, 161, 110, TFT_WHITE);
    M5.Lcd.drawLine(ohx - 1, ohy, 159, 110, TFT_WHITE);
    M5.Lcd.drawLine(ohx, ohy - 1, 160, 109, TFT_WHITE);
    M5.Lcd.drawLine(ohx, ohy + 1, 160, 111, TFT_WHITE);
    M5.Lcd.drawLine(omx, omy, 160, 110, TFT_WHITE);
    M5.Lcd.drawLine(osx, osy, 160, 110, TFT_RED);

    M5.Lcd.fillCircle(160, 110, 3, TFT_RED);

    // draw angle arrow
    int ay = 0;
    if (ns.arrowAngle >= 45)
      ay = 4;
    else if (ns.arrowAngle > -45)
      ay = 18;
    else
      ay = 30;
    M5.Lcd.fillRect(262, 80, 58, 60, TFT_BLACK);
    if (ns.arrowAngle != 180)
      drawArrow(280, ay + 90, 10, ns.arrowAngle + 85, 28, 28, glColor);

    handleAlarmsInfoLine(&ns);
    drawBatteryStatus(icon_xpos[2], icon_ypos[2]);
    drawLogWarningIcon();
  }
  break;

  case MAX_PAGE:
  {
    // display error log
    char tmpStr[32];
    HTTPClient http;
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 18);
    M5.Lcd.setTextDatum(TL_DATUM);
    M5.Lcd.setFreeFont(FMB9);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.drawString("Date  Time  Error Log", 0, 0, GFXFF);
    // M5.Lcd.drawString("Error", 143, 0, GFXFF);
    M5.Lcd.setFreeFont(FM9);
    if (err_log_ptr == 0)
    {
      M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      M5.Lcd.drawString("no errors in log", 0, 20, GFXFF);
    }
    else
    {
      int maxErrDisp = err_log_ptr;
      if (maxErrDisp > 9)
        maxErrDisp = 9;
      for (int i = 0; i < maxErrDisp; i++)
      {
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        sprintf(tmpStr, "%02d.%02d.%02d:%02d", err_log[i].err_time.tm_mday, err_log[i].err_time.tm_mon + 1, err_log[i].err_time.tm_hour, err_log[i].err_time.tm_min);
        M5.Lcd.drawString(tmpStr, 0, 20 + i * 18, GFXFF);
        if (err_log[i].err_code < 0)
        {
          M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
          strlcpy(tmpStr, http.errorToString(err_log[i].err_code).c_str(), 32);
        }
        else
        {
          M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
          switch (err_log[i].err_code)
          {
          case 1001:
            strcpy(tmpStr, "JSON parsing failed");
            break;
          case 1002:
            strcpy(tmpStr, "No data from Server");
            break;
          case 1003:
            strcpy(tmpStr, "JSON2 parsing failed");
            break;
          default:
            sprintf(tmpStr, "HTTP error %d", err_log[i].err_code);
          }
        }
        M5.Lcd.drawString(tmpStr, 132, 20 + i * 18, GFXFF);
      }
      M5.Lcd.setFreeFont(FMB9);
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      sprintf(tmpStr, "Total errors %d", err_log_count);
      M5.Lcd.drawString(tmpStr, 0, 20 + err_log_ptr * 18, GFXFF);
    }
    IPAddress ip = WiFi.localIP();
    if (mDNSactive)
      sprintf(tmpStr, "%s.local (%u.%u.%u.%u)", cfg.deviceName, ip[0], ip[1], ip[2], ip[3]);
    else
      sprintf(tmpStr, "IP Address: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    M5.Lcd.drawString(tmpStr, 0, 20 + 10 * 18, GFXFF);
    handleAlarmsInfoLine(&ns);
    drawBatteryStatus(icon_xpos[2], icon_ypos[2]);
    drawLogWarningIcon();
  }
  break;
  }
}

void serverForConfig()
{
  configManager.setAPName("TomatoM5");
  configManager.setAPFilename("/index.html");

  configManager.addParameterGroup("TomatoServer", new Metadata("Tomato Remote Configuration", "Configuration of Tomato remote shareID"))
      .addParameter("tomatoShareID", config.tomatoShareID, 128, new Metadata("ShareID"));
  configManager.addParameterGroup("Nightscout", new Metadata("Nightscout Configuration", "Configuration of Nightscout URL and tokens"))
      .addParameter("server", config.nSserver, 128, new Metadata("Server"))
      .addParameter("token", config.token, 64, new Metadata("Token"))
      .addParameter("warnningLow", &config.warningLow, new Metadata("Low Target", "Use the same unit as your nightscout value"))
      .addParameter("warnningHigh", &config.warningHigh, new Metadata("High Target", "Use the same unit as your nightscout value"))
      .addParameter("alarmLow", &config.alarmLow, new Metadata("Very Low Target", "Use the same unit as your nightscout value, Will alarm when the bg lower than this!"))
      .addParameter("alarmHigh", &config.alarmHigh, new Metadata("Very High Target", "Use the same unit as your nightscout value,Will alarm when the bg higher than this!"));
  configManager.addParameterGroup("General", new Metadata("General Configuration", "Configuration of target or timezone"))
      .addParameter("timezone", &config.timezone, new Metadata("Time Zone", "timezone Like -1,0,1,2"))
      .addParameter("dataSource", &config.dataSource, new Metadata("DataSource ", "1 for Toamto Remote, 2 for NIghtScout."));

  configManager.begin(config);
}

void showGuidelines()
{
  IPAddress ip = WiFi.localIP();
  String apName = WiFi.SSID();
  Serial.print("apName:");
  Serial.println(apName);

  char tmpStr[30];

  if (WiFi.status() != WL_CONNECTED)
  {
    M5.Lcd.drawString("Please connect to the WiFI: ", 20, 20, GFXFF);
    M5.Lcd.drawString("TomatoM5", 20, 50, GFXFF);
    M5.Lcd.drawString("OPEN http://192.168.1.1 ", 20, 90, GFXFF);
    M5.Lcd.drawString("with a browser to set Toamto M5!", 20, 120, GFXFF);
  }
  else
  {
    M5.Lcd.drawString("Please connect to the WiFI: ", 20, 20, GFXFF);
    M5.Lcd.drawString(apName, 20, 50, GFXFF);
    sprintf(tmpStr, "OPEN http://%u.%u.%u.%u/settings", ip[0], ip[1], ip[2], ip[3]);
    M5.Lcd.drawString(tmpStr, 20, 90, GFXFF);
    M5.Lcd.drawString("with a browser to set Toamto M5!", 20, 120, GFXFF);
  };
}

void webConfigPortal()
{
  serverForConfig();
  if (WiFi.status() != WL_CONNECTED)
  {
    M5.Lcd.fillScreen(BLACK);
    showGuidelines();
  }
}

void startupLogo()
{
  // static uint8_t brightness, pre_brightness;
  M5.Lcd.setBrightness(0);
  if (cfg.bootPic[0] == 0)
  {

    M5.Lcd.drawString("Tomato M5 STack Monitor", 60, 80, GFXFF);
  }
  else
  {
    M5.Lcd.drawJpgFile(SPIFFS, cfg.bootPic);
  }
  M5.Lcd.setBrightness(100);
  M5.update();
}

int8_t getBatteryLevel()
{
  Wire.beginTransmission(0x75);
  Wire.write(0x78);
  if (Wire.endTransmission(false) == 0 && Wire.requestFrom(0x75, 1))
  {
    int8_t bdata = Wire.read();
    /* 
    // write battery info to logfile.txt
    File fileLog = SD.open("/logfile.txt", FILE_WRITE);    
    if(!fileLog) {
      Serial.println("Cannot write to logfile.txt");
    } else {
      int pos = fileLog.seek(fileLog.size());
      struct tm timeinfo;
      getLocalTime(&timeinfo);
      fileLog.print(asctime(&timeinfo));
      fileLog.print("   Battery level: "); fileLog.println(bdata, HEX);
      fileLog.close();
      Serial.print("Log file written: "); Serial.print(asctime(&timeinfo));
    }
    */
    switch (bdata & 0xF0)
    {
    case 0xE0:
      return 25;
    case 0xC0:
      return 50;
    case 0x80:
      return 75;
    case 0x00:
      return 100;
    default:
      return 0;
    }
  }
  return -1;
}

void showConfigLog()
{
  Serial.print("config.nSenabled:");
  Serial.println(config.nSenabled);
  Serial.print("config.token:");
  Serial.println(config.token);
  Serial.print("config.tomatoShareID:");
  Serial.println(config.tomatoShareID);
  Serial.print("config.alarmHigh:");
  Serial.println(config.alarmHigh);
  Serial.print("config.warningHigh:");
  Serial.println(config.warningHigh);
  Serial.print("config.alarmLow:");
  Serial.println(config.alarmLow);
  Serial.print("config.warnningLow:");
  Serial.println(config.warningLow);
}

void setup()
{
  M5.begin();
  // prevent button A "ghost" random presses
  Wire.begin();
  Serial.begin(115200);
  SPIFFS.begin(true);
  if (!SPIFFS.begin())
    while (1)
      Serial.println("SPIFFS.begin() failed");
  M5.Lcd.println("SPIFFS.begin() failed");

  M5.Speaker.mute();

  Serial.println("Ready.");

  // Lcd display
  M5.Lcd.setBrightness(100);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  yield();

  Serial.print("Free Heap: ");
  Serial.println(ESP.getFreeHeap());
  readConfiguration(iniFilename, &cfg);

  if (cfg.invert_display != -1)
  {
    M5.Lcd.invertDisplay(cfg.invert_display);
    Serial.print("Calling M5.Lcd.invertDisplay(");
    Serial.print(cfg.invert_display);
    Serial.println(")");
  }
  else
  {
    Serial.println("No invert_display defined in INI.");
  }
  M5.Lcd.setRotation(cfg.display_rotation);
  lcdBrightness = cfg.brightness1;
  M5.Lcd.setBrightness(lcdBrightness);

  startupLogo();
  delay(2000);
  yield();
  M5.Lcd.setBrightness(lcdBrightness);
  Serial.print("Free Heap = ");
  Serial.println(ESP.getFreeHeap());

  if (WiFi.status() != WL_CONNECTED)
  {
    M5.Lcd.setBrightness(lcdBrightness);
    webConfigPortal();
  }

  if (config.timezone > 12 || config.timezone < -12)
  {
    M5.Lcd.fillScreen(BLACK);
    showGuidelines();
  }

  showConfigLog();
  copyConfig(&cfg);

  // yield();
  if (WiFi.status() == WL_CONNECTED && (config.timezone <= 12 && config.timezone >= -12))
  {
    getDevicetime();
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setBrightness(lcdBrightness);

    dispPage = cfg.default_page;
    setPageIconPos(dispPage);
    // stat startup time
    msStart = millis();
    // update glycemia now
    msCount = msStart - 16000;
  }
}

void loop()
{
  configManager.loop();
  // -- doLoop should be called as frequently as possible.
  buttons_test();

  // update glycemia every 15s
  if (WiFi.status() == WL_CONNECTED && (config.timezone <= 12 && config.timezone >= -12))
  {
    if (millis() - msCount > 15000)
    {
      /* if(dispPage==2)
      M5.Lcd.drawLine(osx, osy, 160, 111, TFT_BLACK); // erase seconds hand while updating data
    */
      if (config.dataSource != 2)
      {
        readTomatoRemote(config.tomatoShareID, &ns);
      }
      else
      {
        // readNssettings(cfg.url, cfg.token);
        readNightscout(cfg.url, cfg.token, &ns);
      };
      // readNightscout(cfg.url, cfg.token, &ns);
      // tomato  ot nightscout
      draw_page();
      msCount = millis();
      Serial.print("msCount = ");
      Serial.println(msCount);
    }
    else
    {
      if ((cfg.restart_at_logged_errors > 0) && (err_log_count >= cfg.restart_at_logged_errors))
      {
        Serial.println("Restarting on number of logged errors...");
        delay(500);
        preferences.begin("M5StackNS", false);
        preferences.putBool("SoftReset", true);
        preferences.putUInt("LastSnoozeTime", lastSnoozeTime);
        preferences.end();
        ESP.restart();
      }
      char lastResetTime[10];
      strcpy(lastResetTime, "Unknown");
      if (getLocalTime(&localTimeInfo))
      {
        sprintf(localTimeStr, "%02d:%02d", localTimeInfo.tm_hour, localTimeInfo.tm_min);
        // no soft restart less than a minute from last restart to prevent several restarts in the same minute
        if ((millis() - msStart > 60000) && (strcmp(cfg.restart_at_time, localTimeStr) == 0))
        {
          Serial.println("Restarting on preset time...");
          delay(500);
          preferences.begin("M5StackNS", false);
          preferences.putBool("SoftReset", true);
          preferences.putUInt("LastSnoozeTime", lastSnoozeTime);
          preferences.end();
          ESP.restart();
        }
      }
      if ((dispPage == 0) && cfg.show_current_time)
      {
        // update current time on display
        M5.Lcd.setFreeFont(FSSB12);
        M5.Lcd.setTextSize(1);
        M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        if (getLocalTime(&localTimeInfo))
        {
          switch (cfg.date_format)
          {
          case 1:
            sprintf(localTimeStr, "%02d:%02d  %d/%d  ", localTimeInfo.tm_hour, localTimeInfo.tm_min, localTimeInfo.tm_mon + 1, localTimeInfo.tm_mday);
            break;
          default:
            sprintf(localTimeStr, "%02d:%02d  %d.%d.  ", localTimeInfo.tm_hour, localTimeInfo.tm_min, localTimeInfo.tm_mday, localTimeInfo.tm_mon + 1);
          }
        }
        else
        {
          strcpy(localTimeStr, "??:??");
          lastMin = 61;
        }
        if (lastMin != localTimeInfo.tm_min)
        {
          lastSec = localTimeInfo.tm_sec;
          lastMin = localTimeInfo.tm_min;
          M5.Lcd.drawString(localTimeStr, 0, 0, GFXFF);
        }
      }
      if (dispPage == 2)
      {
        if (getLocalTime(&localTimeInfo))
        {
          // sprintf(localTimeStr, "%02d:%02d:%02d", localTimeInfo.tm_hour, localTimeInfo.tm_min, localTimeInfo.tm_sec);
        }
        else
        {
          lastMin = 61;
          lastSec = 61;
        }
        if (lastMin != localTimeInfo.tm_min || lastSec != localTimeInfo.tm_sec)
        {
          lastSec = localTimeInfo.tm_sec;
          lastMin = localTimeInfo.tm_min;

          float sx = 0, sy = 1, mx = 1, my = 0, hx = -1, hy = 0; // Saved H, M, S x & y multipliers
          float sdeg = 0, mdeg = 0, hdeg = 0;

          uint8_t hh = localTimeInfo.tm_hour, mm = localTimeInfo.tm_min, ss = localTimeInfo.tm_sec; // Get current time

          // Pre-compute hand degrees, x & y coords for a fast screen update
          sdeg = ss * 6;                     // 0-59 -> 0-354
          mdeg = mm * 6 + sdeg * 0.01666667; // 0-59 -> 0-360 - includes seconds
          hdeg = hh * 30 + mdeg * 0.0833333; // 0-11 -> 0-360 - includes minutes and seconds
          hx = cos((hdeg - 90) * 0.0174532925);
          hy = sin((hdeg - 90) * 0.0174532925);
          mx = cos((mdeg - 90) * 0.0174532925);
          my = sin((mdeg - 90) * 0.0174532925);
          sx = cos((sdeg - 90) * 0.0174532925);
          sy = sin((sdeg - 90) * 0.0174532925);

          if (ss == 0 || initial)
          {
            initial = 0;
            // Erase hour and minute hand positions every minute
            M5.Lcd.drawLine(ohx, ohy, 160, 110, TFT_BLACK);
            M5.Lcd.drawLine(ohx + 1, ohy, 161, 110, TFT_BLACK);
            M5.Lcd.drawLine(ohx - 1, ohy, 159, 110, TFT_BLACK);
            M5.Lcd.drawLine(ohx, ohy - 1, 160, 109, TFT_BLACK);
            M5.Lcd.drawLine(ohx, ohy + 1, 160, 111, TFT_BLACK);
            ohx = hx * 52 + 160;
            ohy = hy * 52 + 110;
            M5.Lcd.drawLine(omx, omy, 160, 110, TFT_BLACK);
            omx = mx * 74 + 160;
            omy = my * 74 + 110;
          }

          // erase old seconds hand position
          M5.Lcd.drawLine(osx, osy, 160, 110, TFT_BLACK);

          // draw day
          M5.Lcd.drawRoundRect(182, 97, 36, 26, 7, TFT_LIGHTGREY);
          M5.Lcd.setTextDatum(MC_DATUM);
          M5.Lcd.setFreeFont(FSSB9);
          M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
          M5.Lcd.drawString(String(localTimeInfo.tm_mday), 200, 108, GFXFF);

          // draw name
          M5.Lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
          M5.Lcd.drawString(cfg.userName, 160, 145, GFXFF);

          // draw digital time
          // M5.Lcd.drawString(localTimeStr, 160, 75, GFXFF);

          // Redraw new hand positions, hour and minute hands not erased here to avoid flicker
          osx = sx * 78 + 160;
          osy = sy * 78 + 110;
          // M5.Lcd.drawLine(osx, osy, 160, 110, TFT_RED);
          M5.Lcd.drawLine(ohx, ohy, 160, 110, TFT_WHITE);
          M5.Lcd.drawLine(ohx + 1, ohy, 161, 110, TFT_WHITE);
          M5.Lcd.drawLine(ohx - 1, ohy, 159, 110, TFT_WHITE);
          M5.Lcd.drawLine(ohx, ohy - 1, 160, 109, TFT_WHITE);
          M5.Lcd.drawLine(ohx, ohy + 1, 160, 111, TFT_WHITE);
          M5.Lcd.drawLine(omx, omy, 160, 110, TFT_WHITE);
          M5.Lcd.drawLine(osx, osy, 160, 110, TFT_RED);

          M5.Lcd.fillCircle(160, 110, 3, TFT_RED);
        }
      }
    }
  }
  else
  {
    return;
  }
  M5.update();
}