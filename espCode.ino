#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include <SD.h>

// =====================
// WIFI INFO
// =====================
const char* ssid = "Maxplanter";
const char* password = "passedwords";

// Google Apps Script Web App URL
const char* serverUrl = "https://script.google.com/macros/s/AKfycbwIWFOveXHV3eLVIekOrm9UWTOOtIx5Z9pIXdjs-gjWBSkWDul6n0YkzDIt-SDAlHk/exec";

// =====================
// ESP8266 SOFTWARE SERIAL PINS
// =====================
// GPIO5 = D1 = ESP RX from Arduino D6 through divider
// GPIO4 = D2 = ESP TX to Arduino D5
#define ARDUINO_RX_PIN 5
#define ARDUINO_TX_PIN 4

SoftwareSerial arduinoSerial(ARDUINO_RX_PIN, ARDUINO_TX_PIN);

// =====================
// MICROSD SETTINGS
// =====================
// SPI pins:
// D5/GPIO14 = SCK
// D6/GPIO12 = MISO
// D7/GPIO13 = MOSI
// D0/GPIO16 = CS
#define SD_CS_PIN 16

bool sdReady = false;

// =====================
// DATA VARIABLES
// =====================
String latestLine = "";

String moisture = "";
String moistureRaw = "";
String threshold = "";
String airTemp = "";
String humidity = "";
String soilTemp = "";
String light = "";
String battery = "";
String waterPump = "";
String filterPump = "";
String autoMode = "";

// Send/log interval
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 10000; // 10 seconds

// =====================
// FUNCTION DECLARATIONS
// =====================
void setupSD();
void parseArduinoLine(String line);
String getValue(String line, String key);
int sendData();
void logToSD(int httpCode);
String urlEncode(String str);

void setup() {
  Serial.begin(115200);
  delay(1000);

  arduinoSerial.begin(9600);

  Serial.println();
  Serial.println("ESP8266 Smart Garden starting...");
  Serial.println("Arduino RX on D1/GPIO5");
  Serial.println("SD card CS on D0/GPIO16");

  setupSD();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("ESP IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    delay(1000);
    return;
  }

  // Read one line from Arduino
  if (arduinoSerial.available()) {
    latestLine = arduinoSerial.readStringUntil('\n');
    latestLine.trim();

    if (latestLine.length() > 0) {
      Serial.print("Received from Arduino: ");
      Serial.println(latestLine);

      parseArduinoLine(latestLine);
    }
  }

  // Send/log every 10 seconds if we have data
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = millis();

    if (latestLine.length() > 0) {
      int httpCode = -999;

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Sending to Google Apps Script...");
        httpCode = sendData();
      }

      Serial.print("HTTP code: ");
      Serial.println(httpCode);

      logToSD(httpCode);
    } else {
      Serial.println("No Arduino data yet.");
    }
  }
}

void setupSD() {
  if (SD.begin(SD_CS_PIN)) {
    sdReady = true;
    Serial.println("SD OK");

    if (!SD.exists("/garden.csv")) {
      File file = SD.open("/garden.csv", FILE_WRITE);

      if (file) {
        file.println("time_ms,moisture,moisture_raw,threshold,air_temp_c,humidity_percent,soil_temp_c,light_lux,battery_v,water_pump,filter_pump,auto_mode,http_code,raw_line");
        file.close();
      }
    }
  } else {
    sdReady = false;
    Serial.println("SD failed/not connected");
  }
}

void parseArduinoLine(String line) {
  moisture = getValue(line, "MOIST=");
  moistureRaw = getValue(line, "RAW=");
  threshold = getValue(line, "THRESH=");
  airTemp = getValue(line, "AIR=");
  humidity = getValue(line, "HUM=");
  soilTemp = getValue(line, "SOIL=");
  light = getValue(line, "LIGHT=");
  battery = getValue(line, "BATT=");
  waterPump = getValue(line, "WATERPUMP=");
  filterPump = getValue(line, "FILTERPUMP=");
  autoMode = getValue(line, "AUTO=");
}

String getValue(String line, String key) {
  int startIndex = line.indexOf(key);

  if (startIndex == -1) {
    return "";
  }

  startIndex += key.length();

  int endIndex = line.indexOf(",", startIndex);

  if (endIndex == -1) {
    endIndex = line.length();
  }

  return line.substring(startIndex, endIndex);
}

int sendData() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String url = String(serverUrl);

  url += "?moisture=" + urlEncode(moisture);
  url += "&moistureRaw=" + urlEncode(moistureRaw);
  url += "&threshold=" + urlEncode(threshold);
  url += "&airTemp=" + urlEncode(airTemp);
  url += "&humidity=" + urlEncode(humidity);
  url += "&soilTemp=" + urlEncode(soilTemp);
  url += "&light=" + urlEncode(light);
  url += "&battery=" + urlEncode(battery);
  url += "&waterPump=" + urlEncode(waterPump);
  url += "&filterPump=" + urlEncode(filterPump);
  url += "&autoMode=" + urlEncode(autoMode);
  url += "&raw=" + urlEncode(latestLine);

  Serial.print("URL: ");
  Serial.println(url);

  http.begin(client, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();

  if (httpCode > 0) {
    String response = http.getString();
    Serial.print("Server response: ");
    Serial.println(response);
  } else {
    Serial.print("HTTP error: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();

  return httpCode;
}

void logToSD(int httpCode) {
  if (!sdReady) {
    return;
  }

  File file = SD.open("/garden.csv", FILE_WRITE);

  if (file) {
    file.print(millis());
    file.print(",");

    file.print(moisture);
    file.print(",");

    file.print(moistureRaw);
    file.print(",");

    file.print(threshold);
    file.print(",");

    file.print(airTemp);
    file.print(",");

    file.print(humidity);
    file.print(",");

    file.print(soilTemp);
    file.print(",");

    file.print(light);
    file.print(",");

    file.print(battery);
    file.print(",");

    file.print(waterPump);
    file.print(",");

    file.print(filterPump);
    file.print(",");

    file.print(autoMode);
    file.print(",");

    file.print(httpCode);
    file.print(",");

    file.print("\"");
    file.print(latestLine);
    file.println("\"");

    file.close();
  }
}

String urlEncode(String str) {
  String encoded = "";

  for (int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);

    if (isalnum(c)) {
      encoded += c;
    } else if (c == '.' || c == '-' || c == '_') {
      encoded += c;
    } else {
      encoded += '%';

      char code0 = (c >> 4) & 0xF;
      char code1 = c & 0xF;

      encoded += char(code0 > 9 ? code0 - 10 + 'A' : code0 + '0');
      encoded += char(code1 > 9 ? code1 - 10 + 'A' : code1 + '0');
    }
  }

  return encoded;
}
