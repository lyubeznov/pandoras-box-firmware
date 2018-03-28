#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

#define FASTLED_ESP8266_RAW_PIN_ORDER

#include "FastLED.h"

const char* ssid = "Pandora's Box";
const char* password = "666pand0ra666";

IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;

AsyncWebServer server(80);

const int LEDS_PORT = 2;
const int LEDS_COUNT = 91;

const uint8_t maxBrightness = 255;

CRGB leds[LEDS_COUNT];

void configureDNS();
void configureOTA();
void configureLeds();
void configureServer();

void getConfiguration(JsonObject &buffer);
bool initConfiguration();
bool loadConfiguration();
bool saveConfiguration(JsonObject& configuration);

void handleRequest(AsyncWebServerRequest *request, uint8_t *data);
void handleCommand(JsonObject &root);

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
  configureDNS();
  configureOTA();
  configureServer();

  Serial.println("Ready");
}

void loop() {
  dnsServer.processNextRequest();
  ArduinoOTA.handle();
  
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
  
  delay(10);
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
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    DynamicJsonBuffer responseBuffer;
    JsonObject& responseObject = responseBuffer.createObject();
    
    handleRequest(request, data, responseObject);
    
    String responseText;
    responseObject.prettyPrintTo(responseText);
    
    if (responseObject.containsKey("error")) {
      int errorCode = responseObject["errorCode"];
      
      request->send(errorCode, "application/json", responseText);
    } else {
      request->send(200, "application/json", responseText);
    }
  });

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

void handleRequest(AsyncWebServerRequest *request, uint8_t *data, JsonObject &response) {  
  if (request->url() == "/command") {
    DynamicJsonBuffer requestBuffer;
    JsonObject &requestObject = requestBuffer.parseObject((const char*) data);
    
    if (!requestObject.success()) {
      response["errorCode"] = 422;
      response["error"] = "Unprocessable Entity";
    } else {
      handleCommand(requestObject, response);
    }
  } else {
    response["errorCode"] = 404;
    response["error"] = "Not Found";
  }
}

JsonObject& handleCommand(JsonObject &request, JsonObject &response) {  
  if (!request.containsKey("type")) {
    response["errorCode"] = 400;
    response["error"] = "Bad Request";
  } else {
    const char *command = request["type"];

    if (strcmp(command, "PING") == 0) {
      response["pong"] = request["payload"];
    } else if (strcmp(command, "POWER_ON") == 0) {
      double brightness = 0.1;
      
      setBrightness(brightness);
  
      response.set("brightness", brightness);
    } else if (strcmp(command, "POWER_OFF") == 0) {
      double brightness = request.get<double>("brightness");
      
      setBrightness(brightness);
  
      response.set("brightness", brightness);
    } else if (strcmp(command, "SET_BRIGHTNESS") == 0) {
      double brightness = request.get<double>("brightness");
      
      setBrightness(brightness);
  
      response.set("brightness", brightness);
    } else if (strcmp(command, "SET_COLOR") == 0) {
      JsonObject& color = request["color"];
      
      double h = color.get<double>("h");
      double s = color.get<double>("s");
      double v = color.get<double>("v");
      
      setColor(h, s, v);
  
      JsonObject& responseColor = response.createNestedObject("color");
      
      responseColor.set("h", h);
      responseColor.set("s", s);
      responseColor.set("v", v);
    } else if (strcmp(command, "GET_CONFIGURATION") == 0) {
      getConfiguration(response);
    } else if (strcmp(command, "SET_CONFIGURATION") == 0) {
      JsonObject& color = request["color"];
      
      double h = color.get<double>("h");
      double s = color.get<double>("s");
      double v = color.get<double>("v");

      double brightness = request.get<double>("brightness");
      
      setColor(h, s, v);
      setBrightness(brightness);

      JsonObject& responseColor = response.createNestedObject("color");
      
      responseColor.set("h", h);
      responseColor.set("s", s);
      responseColor.set("v", v);

      response.set("brightness", brightness);
    } else if (strcmp(command, "SET_DEFAULT") == 0) {
      getConfiguration(response);
      saveConfiguration(response);
    } 
  }
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

  return saveConfiguration(configuration);
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
    return reinitConfiguration(configFile);
  }

  if (!root.containsKey("brightness") || !root.containsKey("color")) {    
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
  
  return true;
}

double mapDouble(double x, double in_min, double in_max, double out_min, double out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

