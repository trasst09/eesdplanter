#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SoftwareSerial.h>

// =====================
// WIFI INFO
// =====================
const char* ssid = "XieXieXieXie";
const char* password = "Donghong.Wenhao.2025";

// Google Apps Script Web App URL
const char* serverUrl = "https://script.google.com/macros/s/AKfycbwIWFOveXHV3eLVIekOrm9UWTOOtIx5Z9pIXdjs-gjWBSkWDul6n0YkzDIt-SDAlHk/exec";

// =====================
// ESP8266 SOFTWARE SERIAL PINS
// =====================
// GPIO14 = D5 = ESP RX from Arduino D6
// GPIO12 = D6 = ESP TX to Arduino D5
#define ARDUINO_RX_PIN 14
#define ARDUINO_TX_PIN 12

SoftwareSerial arduinoSerial(ARDUINO_RX_PIN, ARDUINO_TX_PIN);

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

// Send interval
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 10000; // 10 seconds

// =====================
// FUNCTION DECLARATIONS
// =====================
void parseArduinoLine(String line);
String getValue(String line, String key);
int sendData();
String urlEncode(String str);

void setup() {
  Serial.begin(115200);
  delay(1000);

  arduinoSerial.begin(9600);

  Serial.println();
  Serial.println("ESP8266 Smart Garden WiFi starting...");
  Serial.println("Waiting for Arduino data on ESP D5/GPIO14...");

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

      Serial.println("Parsed:");
      Serial.print("moisture = "); Serial.println(moisture);
      Serial.print("raw = "); Serial.println(moistureRaw);
      Serial.print("threshold = "); Serial.println(threshold);
      Serial.print("airTemp = "); Serial.println(airTemp);
      Serial.print("humidity = "); Serial.println(humidity);
      Serial.print("soilTemp = "); Serial.println(soilTemp);
      Serial.print("light = "); Serial.println(light);
      Serial.print("battery = "); Serial.println(battery);
      Serial.print("waterPump = "); Serial.println(waterPump);
      Serial.print("filterPump = "); Serial.println(filterPump);
      Serial.print("autoMode = "); Serial.println(autoMode);
    }
  }

  // Send every 10 seconds if we have data
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = millis();

    if (latestLine.length() > 0 && WiFi.status() == WL_CONNECTED) {
      Serial.println("Sending data to Google Apps Script...");

      int httpCode = sendData();

      Serial.print("HTTP code: ");
      Serial.println(httpCode);

      if (httpCode == 200) {
        Serial.println("Upload likely successful.");
      } else {
        Serial.println("Upload may have failed.");
      }
    } else {
      Serial.println("No Arduino data yet, so not sending.");
    }
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
