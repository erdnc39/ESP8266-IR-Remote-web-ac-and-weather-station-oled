#include "Web-AC-control.h"
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#endif  // ESP8266
#if defined(ESP32)
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>
#endif  // ESP32
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <coredecls.h>                  // settimeofday_cb()
#else
#include <WiFi.h>
#endif
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier
#include "SSD1306Wire.h" 
#include <Wire.h>
#include <ESPHTTPClient.h>
#include <JsonListener.h>
#include <ir_Coolix.h>  //  replace library based on your AC unit model, check https://github.com/crankyoldgit/IRremoteESP8266
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include "OLEDDisplayUi.h"
#include "OpenWeatherMapCurrent.h"
#include "OpenWeatherMapForecast.h"
#include "WeatherStationFonts.h"
#include "WeatherStationImages.h"
#include "DHTesp.h"
DHTesp dht;
#include <ESPHTTPClient.h>
#include <JsonListener.h>

#define TZ              2       // (utc+) TZ in hours
#define DST_MN          60      // use 60mn for summer time in some countries

const int UPDATE_INTERVAL_SECS = 20 * 60; // Update every 20 minutes

const int I2C_DISPLAY_ADDRESS = 0x3c;
#if defined(ESP8266)
const int SDA_PIN = 4;
const int SDC_PIN = 5;
#else
const int SDA_PIN = 4; //D3;
const int SDC_PIN = 5; //D4;
#endif

String OPEN_WEATHER_MAP_APP_ID = "92cf98c11e7923d668ae73d44d8f126e";
String OPEN_WEATHER_MAP_LOCATION_ID = "745042";
String OPEN_WEATHER_MAP_LANGUAGE = "tr";
const uint8_t MAX_FORECASTS = 4;
const boolean IS_METRIC = true;
const String WDAY_NAMES[] = {"PAZ", "PTS", "SAL", "CAR", "PER", "CUM", "CTS"};
const String MONTH_NAMES[] = {"OCK", "SUB", "MAR", "NIS", "MAY", "HAZ", "TEM", "AGU", "EYL", "EKI", "KAS", "ARL"};

SSD1306Wire     display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
 OLEDDisplayUi   ui( &display );

OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapCurrent currentWeatherClient;

OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
OpenWeatherMapForecast forecastClient;

#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)
time_t now;

// flag changed in the ticker function every 10 minutes
bool readyForWeatherUpdate = false;

String lastUpdate = "--";

long timeSinceLastWUpdate = 0;

//declaring prototypes
void drawProgress(OLEDDisplay *display, int percentage, String label);
void updateData(OLEDDisplay *display);
void drawDth(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawWifi(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state);
void setReadyForWeatherUpdate();

FrameCallback frames[] = { drawDth, drawDateTime, drawCurrentWeather, drawForecast, drawWifi};
int numberOfFrames = 5;

OverlayCallback overlays[] = { drawHeaderOverlay };
int numberOfOverlays = 1;



#define AUTO_MODE kCoolixAuto
#define COOL_MODE kCoolixCool
#define DRY_MODE kCoolixDry
#define HEAT_MODE kCoolixHeat
#define FAN_MODE kCoolixFan

#define FAN_AUTO kCoolixFanAuto
#define FAN_MIN kCoolixFanMin
#define FAN_MED kCoolixFanMed
#define FAN_HI kCoolixFanMax

// ESP8266 GPIO pin to use for IR blaster.
const uint16_t kIrLed = 15;
// Library initialization, change it according to the imported library file.
IRCoolixAC ac(kIrLed);

/// ##### End user configuration ######


struct state {
  uint8_t temperature = 22, fan = 0, operation = 0;
  bool powerStatus;
};

File fsUploadFile;

// core

state acState;

// settings
char deviceName[] = "AC Remote Control";

#if defined(ESP8266)
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdateServer;
#endif  // ESP8266
#if defined(ESP32)
WebServer server(80);
#endif  // ESP32

bool handleFileRead(String path) {
  //  send the right file to the client (if it exists)
   Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";
  // If a folder is requested, send the index file
  String contentType = getContentType(path);
  // Get the MIME type
  String pathWithGz = path + ".gz";
  if (FILESYSTEM.exists(pathWithGz) || FILESYSTEM.exists(path)) {
    // If the file exists, either as a compressed archive, or normal
    // If there's a compressed version available
    if (FILESYSTEM.exists(pathWithGz))
      path += ".gz";  // Use the compressed verion
    File file = FILESYSTEM.open(path, "r");
    //  Open the file
    server.streamFile(file, contentType);
    //  Send it to the client
    file.close();
    // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
   Serial.println(String("\tFile Not Found: ") + path);
  // If the file doesn't exist, return false
  return false;
}

String getContentType(String filename) {
  // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

void handleFileUpload() {  // upload a new file to the FILESYSTEM
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
     Serial.print("handleFileUpload Name: "); //Serial.println(filename);
    fsUploadFile = FILESYSTEM.open(filename, "w");
    // Open the file for writing in FILESYSTEM (create if it doesn't exist)
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
      // Write the received bytes to the file
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      // If the file was successfully created
      fsUploadFile.close();
      // Close the file again
       Serial.print("handleFileUpload Size: ");
       Serial.println(upload.totalSize);
      server.sendHeader("Location", "/success.html");
      // Redirect the client to the success page
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}


void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setup() {
   Serial.begin(115200);
   Serial.println();
   display.init();
  display.clear();
  display.display();

  //display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);
  display.drawString(64, 10, "WiFi Baglaniyor");
   dht.setup(2, DHTesp::DHT11);
  ac.begin();
  
  delay(1000);
  
  Serial.println("mounting " FILESYSTEMSTR "...");

  if (!FILESYSTEM.begin()) {
     Serial.println("Failed to mount file system");
    return;
  }

  WiFiManager wifiManager;

  

  if (!wifiManager.autoConnect(deviceName)) {
    delay(3000);

    ESP.restart();
    delay(5000);
  }


#if defined(ESP8266)
  httpUpdateServer.setup(&server);
#endif  // ESP8266



  server.on("/state", HTTP_PUT, []() {
    DynamicJsonDocument root(1024);
    DeserializationError error = deserializeJson(root, server.arg("plain"));
    if (error) {
      server.send(404, "text/plain", "FAIL. " + server.arg("plain"));
    } else {
      if (root.containsKey("temp")) {
        acState.temperature = (uint8_t) root["temp"];
      }

      if (root.containsKey("fan")) {
        acState.fan = (uint8_t) root["fan"];
      }

      if (root.containsKey("power")) {
        acState.powerStatus = root["power"];
      }

      if (root.containsKey("mode")) {
        acState.operation = root["mode"];
      }

      String output;
      serializeJson(root, output);
      server.send(200, "text/plain", output);

      delay(200);

      if (acState.powerStatus) {
        ac.on();
        ac.setTemp(acState.temperature);
        if (acState.operation == 0) {
          ac.setMode(AUTO_MODE);
          ac.setFan(FAN_AUTO);
          acState.fan = 0;
        } else if (acState.operation == 1) {
          ac.setMode(COOL_MODE);
        } else if (acState.operation == 2) {
          ac.setMode(DRY_MODE);
        } else if (acState.operation == 3) {
          ac.setMode(HEAT_MODE);
        } else if (acState.operation == 4) {
          ac.setMode(FAN_MODE);
        }

        if (acState.operation != 0) {
          if (acState.fan == 0) {
            ac.setFan(FAN_AUTO);
          } else if (acState.fan == 1) {
            ac.setFan(FAN_MIN);
          } else if (acState.fan == 2) {
            ac.setFan(FAN_MED);
          } else if (acState.fan == 3) {
            ac.setFan(FAN_HI);
          }
        }
      } else {
        ac.off();
      }
      ac.send();
    }
  });

  server.on("/file-upload", HTTP_POST,
  // if the client posts to the upload page
  []() {
    // Send status 200 (OK) to tell the client we are ready to receive
    server.send(200);
  },
  handleFileUpload);  // Receive and save the file

  server.on("/file-upload", HTTP_GET, []() {
    // if the client requests the upload page

    String html = "<form method=\"post\" enctype=\"multipart/form-data\">";
    html += "<input type=\"file\" name=\"name\">";
    html += "<input class=\"button\" type=\"submit\" value=\"Upload\">";
    html += "</form>";
    server.send(200, "text/html", html);
  });

  server.on("/", []() {
    server.sendHeader("Location", String("ui.html"), true);
    server.send(302, "text/plain", "");
  });

  server.on("/state", HTTP_GET, []() {
    DynamicJsonDocument root(1024);
    root["mode"] = acState.operation;
    root["fan"] = acState.fan;
    root["temp"] = acState.temperature;
    root["power"] = acState.powerStatus;
    String output;
    serializeJson(root, output);
    server.send(200, "text/plain", output);
  });


  server.on("/reset", []() {
    server.send(200, "text/html", "reset");
    delay(100);
    ESP.restart();
  });
  server.on("/dht", HTTP_GET, []() {
    DynamicJsonDocument doc(1024);
    float temperature = dht.getTemperature();
    float humidity = dht.getHumidity();
    temperature = round(temperature * 10) / 10.0; // 1 ondalık basamak için
    humidity = round(humidity * 10) / 10.0; // 1 ondalık basamak için
    doc["temperature"] = String(temperature, 1); // 1 basamaklı hassasiyet
    doc["humidity"] = String(humidity, 1); // 1 basamaklı hassasiyet    
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  server.serveStatic("/", FILESYSTEM, "/", "max-age=86400");

  server.onNotFound(handleNotFound);

  server.begin();
    configTime(TZ_SEC, DST_SEC, "pool.ntp.org");
  ui.setTargetFPS(30);
  ui.setActiveSymbol(activeSymbole);
  ui.setInactiveSymbol(inactiveSymbole);
  ui.setIndicatorPosition(BOTTOM);
  ui.setIndicatorDirection(LEFT_RIGHT);
  ui.setFrameAnimation(SLIDE_LEFT);
  ui.setFrames(frames, numberOfFrames);

  ui.setOverlays(overlays, numberOfOverlays);
  ui.init();

  Serial.println("");
      int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.clear();
    display.drawString(64, 10, "WiFi Baglaniyor");
    display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbole : inactiveSymbole);
    display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbole : inactiveSymbole);
    display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbole : inactiveSymbole);
    display.display();

    counter++;
  }
  updateData(&display);
}


void loop() {
  
  
  if (millis() - timeSinceLastWUpdate > (1000L*UPDATE_INTERVAL_SECS)) {
    setReadyForWeatherUpdate();
    timeSinceLastWUpdate = millis();
  }

  if (readyForWeatherUpdate && ui.getUiState()->frameState == FIXED) {
    updateData(&display);
  }

  int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0) {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.
    delay(remainingTimeBudget);
  }
  server.handleClient();
}

void drawProgress(OLEDDisplay *display, int percentage, String label) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 10, percentage);
  display->display();
}

void updateData(OLEDDisplay *display) {
  drawProgress(display, 10, "Saat guncelleniyor...");
  drawProgress(display, 30, "Hava Durumu guncelleniyo...");
  currentWeatherClient.setMetric(IS_METRIC);
  currentWeatherClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  currentWeatherClient.updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);
  drawProgress(display, 50, "Hava Tahminleri alınıyor...");
  forecastClient.setMetric(IS_METRIC);
  forecastClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  uint8_t allowedHours[] = {12};
  forecastClient.setAllowedHours(allowedHours, sizeof(allowedHours));
  forecastClient.updateForecastsById(forecasts, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID, MAX_FORECASTS);

  readyForWeatherUpdate = false;
  drawProgress(display, 100, "Tamamlandı...");
  delay(1000);
}



void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[16];


  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String date = WDAY_NAMES[timeInfo->tm_wday];

  sprintf_P(buff, PSTR("%s, %02d/%02d/%04d"), WDAY_NAMES[timeInfo->tm_wday].c_str(), timeInfo->tm_mday, timeInfo->tm_mon+1, timeInfo->tm_year + 1900);
  display->drawString(64 + x, 0 + y, String(buff));
  display->setFont(ArialMT_Plain_24);

  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);
  display->drawString(64 + x, 15 + y, String(buff));
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, 38 + y, currentWeather.description);

  display->setFont(ArialMT_Plain_24);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  String temp = String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F");
  display->drawString(60 + x, 5 + y, temp);

  display->setFont(Meteocons_Plain_36);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(32 + x, 0 + y, currentWeather.iconMeteoCon);
}


void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  drawForecastDetails(display, x, y, 0);
  drawForecastDetails(display, x + 44, y, 1);
  drawForecastDetails(display, x + 88, y, 2);
}

void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex) {
  time_t observationTimestamp = forecasts[dayIndex].observationTime;
  struct tm* timeInfo;
  timeInfo = localtime(&observationTimestamp);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y, WDAY_NAMES[timeInfo->tm_wday]);

  display->setFont(Meteocons_Plain_21);
  display->drawString(x + 20, y + 12, forecasts[dayIndex].iconMeteoCon);
  String temp = String(forecasts[dayIndex].temp, 0) + (IS_METRIC ? "°C" : "°F");
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y + 34, temp);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[14];
  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);

  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 54, String(buff));
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  String temp = String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F");
  display->drawString(128, 54, temp);
  display->drawHorizontalLine(0, 52, 128);
}
void drawDth(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  delay(dht.getMinimumSamplingPeriod());

  float humidity = dht.getHumidity();
  float temperature = dht.getTemperature();
  
  
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_24);
  display->drawString(x + 64, y + 0, String(temperature)+" °C");
  display->drawString(x + 64, y + 25, "% "+String(humidity));
  
  
}

void drawWifi(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  
  int32_t RSSI;
  uint8_t prevRssi;
  prevRssi = 0;
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String ssid = WiFi.SSID(); 
  String ipadress = WiFi.localIP().toString();
  display->drawHorizontalLine(0, 10, 128);
  display->drawString(64,15, ssid); 
  display->drawString(64,30, ipadress); 
  int bars;
//  int bars = map(RSSI,-80,-44,1,6); // this method doesn't refelct the Bars well
  // simple if then to set the number of bars
  
  if (RSSI > -55) { 
    bars = 5;
  } else if (RSSI < -55 & RSSI > -65) {
    bars = 4;
  } else if (RSSI < -65 & RSSI > -70) {
    bars = 3;
  } else if (RSSI < -70 & RSSI > -78) {
    bars = 2;
  } else if (RSSI < -78 & RSSI > -82) {
    bars = 1;
  } else {
    bars = 0;
  }

  
// Do some simple loop math to draw rectangles as the bars
// Draw one bar for each "bar" Duh...
 
  
  
}
void setReadyForWeatherUpdate() {
  Serial.println("Setting readyForUpdate to true");
  readyForWeatherUpdate = true;
}
