#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

#define FASTLED_ESP8266_RAW_PIN_ORDER

#include "FastLED.h"

const char* ssid = "Pandora's Box";
const char* password = "666pand0ra666";

IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;

ESP8266WebServer server(80);

const int LEDS_PORT = 2;
const int LEDS_COUNT = 91;

const uint8_t maxBrightness = 255;

CRGB leds[LEDS_COUNT];

void configureDNS();
void configureOTA();
void configureLeds();
void configureServer();

void handlePing();
void handlePower();
void handleColor();
void handleBrightness();
void handleConfiguration();
void handleDefault();

void sendNotFound();
void sendBadRequest();
void sendOk();
void sendOKJson(JsonObject &buffer);

bool reinitConfiguration(File file);
void getConfiguration(JsonObject &buffer);
bool initConfiguration();
bool loadConfiguration();
bool saveConfiguration(JsonObject& configuration);

uint8_t commandedColorH = 0;
uint8_t commandedColorS = 0;
uint8_t commandedColorV = 0;

uint8_t actualColorH = commandedColorH;
uint8_t actualColorS = commandedColorS;
uint8_t actualColorV = commandedColorV;

uint8_t commandedBrightness = 0;
uint8_t actualBrightness = commandedBrightness;

void setup() {
  configureLeds();

  delay(1000);
  WiFi.disconnect();
  delay(100);
  
  Serial.begin(115200);
  Serial.println("Booting");
  
  Serial.print("Configuring access point...");

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, password);

  SPIFFS.begin();

  loadConfiguration();
  // configureDNS();
  configureOTA();
  configureServer();

  Serial.println("Ready");
}

bool isDataChanged() {
  if (actualColorH != commandedColorH) return true;
  if (actualColorS != commandedColorS) return true;
  if (actualColorV != commandedColorV) return true;

  if (actualBrightness != commandedBrightness) return true;

  return false;
}

void loop() {
  // dnsServer.processNextRequest();
  server.handleClient();
  ArduinoOTA.handle();

  if (isDataChanged()) {
    actualColorH = commandedColorH;
    actualColorS = commandedColorS;
    actualColorV = commandedColorV;
  
    actualBrightness = commandedBrightness;
  
    uint8_t h = actualColorH;
    uint8_t s = actualColorS;
    uint8_t v = actualColorV;
  
    CHSV color = CHSV(h, s, v);
    uint8_t brightness = map(actualBrightness, 0, 255, 0, maxBrightness);
  
    FastLED.showColor(color, brightness);
  }
  
  delay(20);
}

void blink(CRGB onColor, CRGB offColor, uint8_t delayTime, uint8_t times, uint8_t brightness = 10) {
  for (uint8_t i = 0; i < times; i++) {
    delay(delayTime);
    FastLED.showColor(onColor, brightness);
    delay(delayTime);
    FastLED.showColor(offColor, brightness);
  }
}

void configureDNS() {
  dnsServer.setTTL(300);
  dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
  dnsServer.start(53, "pandoras.box", apIP);
}

void configureOTA() {
  ArduinoOTA.onStart([]() {
    SPIFFS.end();

    FastLED.showColor(CRGB::Black, 0);
    
    blink(CRGB::Red, CRGB::Black, 250, 3);
  });
  
  ArduinoOTA.onEnd([]() {
    FastLED.showColor(CRGB::Black, 0);
    
    blink(CRGB::Green, CRGB::Black, 250, 3);
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    FastLED.showColor(CRGB::OrangeRed, 10);
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");
  
  ArduinoOTA.begin();
}

void configureLeds() {  
  FastLED.addLeds<WS2812B, LEDS_PORT, GRB>(leds, LEDS_COUNT).setCorrection(TypicalSMD5050);
  FastLED.showColor(CRGB::Black, 0);
}

void configureServer() {
  server.on("/ping", HTTP_GET, handlePing);
  
  server.on("/power", HTTP_POST, handlePower);
  
  server.on("/color", handleColor);
  server.on("/brightness", handleBrightness);

  server.on("/configuration", handleConfiguration);

  server.on("/default", HTTP_POST, handleDefault);

  server.onNotFound(sendNotFound);

  server.begin();
}

void setColor(double h, double s, double v) {
  commandedColorH = mapDouble(constrain(h, 0, 360), 0, 360, 0, 255);
  commandedColorS = mapDouble(constrain(s, 0, 1), 0, 1, 0, 255);
  commandedColorV = mapDouble(constrain(v, 0, 1), 0, 1, 0, 255);
}

void setBrightness(double brightness) {
  commandedBrightness = mapDouble(constrain(brightness, 0, 1), 0, 1, 0, maxBrightness);
}

void sendNotFound() {
  server.send(404, "text/plain", "Not Found");
}

void sendBadRequest() {
  server.send(400, "text/plain", "Bad request");
}

void sendOk() {
  server.send(200, "text/plain", "OK");
}

void sendOKJson(JsonObject &buffer) {
  String response; 
  buffer.prettyPrintTo(response);

  server.send(200, "application/json", response);
}

void handlePing() {
  server.client().setNoDelay(true);
  
  if (server.hasArg("payload")) {
    server.send(200, "text/plain", server.arg("payload"));
  } else {
    sendBadRequest();
  }
}

void handlePower() {
  server.client().setNoDelay(true);
  
  if (server.hasArg("status")) {
    bool status = server.arg("status") == "on";
    double brightness = status ? 0.1 : 0;
      
    setBrightness(brightness);
    sendOk();
  } else {
    sendBadRequest();
  }
}

void handleColor() {
  server.client().setNoDelay(true);
  
  if (server.method() == HTTP_GET) {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& color = jsonBuffer.createObject();

    color.set("h", mapDouble(commandedColorH, 0, 255, 0, 360));
    color.set("s", mapDouble(commandedColorS, 0, 255, 0, 1));
    color.set("v", mapDouble(commandedColorV, 0, 255, 0, 1));

    sendOKJson(color);
    jsonBuffer.clear();
  } else if (server.method() == HTTP_POST) {
    if (server.hasArg("colorH") && server.hasArg("colorS") && server.hasArg("colorV")) {     
      double h = server.arg("colorH").toFloat();
      double s = server.arg("colorS").toFloat();
      double v = server.arg("colorV").toFloat();
      
      setColor(h, s, v);
      sendOk();
    } else {
      sendBadRequest();
    }
  } else {
    sendBadRequest();
  }
}

void handleBrightness() {
  server.client().setNoDelay(true);
  
  if (server.method() == HTTP_GET) {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& brightness = jsonBuffer.createObject();
    
    brightness.set("brightness", mapDouble(commandedBrightness, 0, 255, 0, 1));

    sendOKJson(brightness);
    jsonBuffer.clear();
  } else if (server.method() == HTTP_POST) {
    if (server.hasArg("brightness")) {     
      double brightness = server.arg("brightness").toFloat();
      
      setBrightness(brightness);
      sendOk();
    } else {
      sendBadRequest();
    }
  } else {
    sendBadRequest();
  }
}

void handleConfiguration() {
  server.client().setNoDelay(true);
  
  DynamicJsonBuffer jsonBuffer;
  JsonObject& configuration = jsonBuffer.createObject();
  
  if (server.method() == HTTP_GET) {
    getConfiguration(configuration);
    sendOKJson(configuration);
  } else if (server.method() == HTTP_POST) {
    if (server.hasArg("brightness") && server.hasArg("colorH") && server.hasArg("colorS") && server.hasArg("colorV")) {     
      double brightness = server.arg("brightness").toFloat();

      double h = server.arg("colorH").toFloat();
      double s = server.arg("colorS").toFloat();
      double v = server.arg("colorV").toFloat();
      
      setBrightness(brightness);
      setColor(h, s, v);

      configuration.set("brightness", brightness);
  
      JsonObject& color = configuration.createNestedObject("color");
    
      color.set("h", h);
      color.set("s", s);
      color.set("v", v);
      
      sendOKJson(configuration);
    } else {
      sendBadRequest();
    }
  } else {
    sendBadRequest();
  }

  jsonBuffer.clear();
}

void handleDefault() {
  server.client().setNoDelay(true);
  
  DynamicJsonBuffer jsonBuffer;
  JsonObject& configuration = jsonBuffer.createObject();

  getConfiguration(configuration);
  saveConfiguration(configuration);

  sendOk();
}

void getConfiguration(JsonObject &buffer) {  
  buffer.set("brightness", mapDouble(commandedBrightness, 0, 255, 0, 1));
  
  JsonObject& color = buffer.createNestedObject("color");

  color.set("h", mapDouble(commandedColorH, 0, 255, 0, 360));
  color.set("s", mapDouble(commandedColorS, 0, 255, 0, 1));
  color.set("v", mapDouble(commandedColorV, 0, 255, 0, 1));
}

bool saveConfiguration(JsonObject& configuration) {
  File configFile = SPIFFS.open("/config.json", "w");

  if (!configFile) {    
    return false;
  }

  configuration.prettyPrintTo(configFile);
  configFile.close();

  return true;
}

bool initConfiguration() {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& configuration = jsonBuffer.createObject();
  
  getConfiguration(configuration);

  bool isConfigurationSaved = saveConfiguration(configuration);

  jsonBuffer.clear();

  return isConfigurationSaved;
}

bool reinitConfiguration(File file) {
  if (file) {
    file.close();
  }
  
  initConfiguration();
    
  return loadConfiguration();
}

bool loadConfiguration() {
  File configFile = SPIFFS.open("/config.json", "r");
  
  if (!configFile) {    
    return reinitConfiguration(configFile);
  }
  
  size_t size = configFile.size();
  
  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(buf.get());
  
  if (!root.success()) {
    jsonBuffer.clear();
    
    return reinitConfiguration(configFile);
  }

  if (!root.containsKey("brightness") || !root.containsKey("color")) {  
    jsonBuffer.clear();
      
    return reinitConfiguration(configFile);
  }

  JsonObject& color = root["color"];

  if (!color.containsKey("h") || !color.containsKey("s") || !color.containsKey("v")) {    
    return reinitConfiguration(configFile);
  }

  double h = color.get<double>("h");
  double s = color.get<double>("s");
  double v = color.get<double>("v");

  double brightness = root.get<double>("brightness");

  setColor(h, s, v);
  setBrightness(brightness);

  jsonBuffer.clear();
  
  return true;
}

double mapDouble(double x, double in_min, double in_max, double out_min, double out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

