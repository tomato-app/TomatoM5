#include <Arduino.h>
#include <M5Stack.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiMulti.h>
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
// #include "M5NSconfig.h"
// #include "M5NSWebConfig.h"
#include "DHT12.h"
#include <Wire.h> //The DHT12 uses I2C comunication.
#include <IotWebConf.h>

DHT12 dht12; //Preset scale CELSIUS and ID 0x5c.

String M5NSversion("20200825");
WiFiMulti WiFiMultiple;

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

// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "TomatoM5";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "";

#define STRING_LEN 128
#define NUMBER_LEN 32

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "dem2"

// -- When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
#define CONFIG_PIN D2

// -- Status indicator pin.
//      First it will light up (kept LOW), on Wifi connection it will blink,
//      when connected to the Wifi it will turn off (kept HIGH).
#define STATUS_PIN LED_BUILTIN

// -- Callback method declarations.
void configSaved();
boolean formValidator();

DNSServer dnsServer;
WebServer server(80);

char stringParamValue[STRING_LEN];
char intParamValue[NUMBER_LEN];
char floatParamValue[NUMBER_LEN];

unsigned long msCount;
unsigned long msCountLog;
unsigned long msStart;
uint8_t lcdBrightness = 10;
const char iniFilename[] = "/M5NS.INI";
tConfig cfg;

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);
IotWebConfParameter stringParam = IotWebConfParameter("String param", "stringParam", stringParamValue, STRING_LEN);
IotWebConfSeparator separator1 = IotWebConfSeparator();
IotWebConfParameter intParam = IotWebConfParameter("Int param", "intParam", intParamValue, NUMBER_LEN, "number", "1..100", NULL, "min='1' max='100' step='1'");
// -- We can add a legend to the separator
IotWebConfSeparator separator2 = IotWebConfSeparator("Calibration factor");
IotWebConfParameter floatParam = IotWebConfParameter("Float param", "floatParam", floatParamValue, NUMBER_LEN, "number", "e.g. 23.4", NULL, "step='0.1'");

/**
 * Handle web requests to "/" path.
 */
void handleRoot()
{
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>IotWebConf 03 Custom Parameters</title></head><body>";
  s += "<ul>";
  s += "<li>String param value: ";
  s += stringParamValue;
  s += "<li>Int param value: ";
  s += atoi(intParamValue);
  s += "<li>Float param value: ";
  s += atof(floatParamValue);
  s += "</ul>";
  s += "Go to <a href='config'>configure page</a> to change values.";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}

void configSaved()
{
  Serial.println("Configuration was updated.");
}

boolean formValidator()
{
  Serial.println("Validating form.");
  boolean valid = true;

  int l = server.arg(stringParam.getId()).length();
  if (l < 3)
  {
    stringParam.errorMessage = "Please provide at least 3 characters for this test!";
    valid = false;
  }

  return valid;
}

void serverForConfig()
{
  server.on("/", handleRoot);
  server.on("/config", [] { iotWebConf.handleConfig(); });
  server.onNotFound([]() { iotWebConf.handleNotFound(); });

  Serial.println("Ready.");
}

void showGuidelines()
{
  if (WiFiMultiple.run() != WL_CONNECTED)
  {
    M5.Lcd.drawString("Please link to the wifi: ", 20, 20, GFXFF);
    M5.Lcd.drawString("TomatoM5", 20, 50, GFXFF);
    M5.Lcd.drawString("         ", 20, 70, GFXFF);
    M5.Lcd.drawString("and then open 192.168.4.1", 20, 80, GFXFF);
  }
}

void webConfigPortal()
{
  iotWebConf.addParameter(&stringParam);
  iotWebConf.addParameter(&separator1);
  iotWebConf.addParameter(&intParam);
  iotWebConf.addParameter(&separator2);
  iotWebConf.addParameter(&floatParam);
  iotWebConf.setConfigSavedCallback(&configSaved);
  iotWebConf.setFormValidator(&formValidator);
  iotWebConf.getApTimeoutParameter()->visible = true;

  // -- Initializing the configuration.
  iotWebConf.init();
  serverForConfig();
  showGuidelines();
  Serial.print(".");

  delay(10000);
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
  
  M5.Lcd.setBrightness(lcdBrightness);
  delay(1000);  

  webConfigPortal();
  yield();
  // -- Set up required URL handlers on the web server.
}

void loop()
{
  // -- doLoop should be called as frequently as possible.
  iotWebConf.doLoop();
}