#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

const char* ssid = "XieXieXieXie";
const char* password = "Donghong.Wenhao.2025";

// Paste your Google Apps Script Web App URL here
const char* serverUrl = "https://script.google.com/macros/s/AKfycbwIWFOveXHV3eLVIekOrm9UWTOOtIx5Z9pIXdjs-gjWBSkWDul6n0YkzDIt-SDAlHk/exec";

void setup() {
  Serial.begin(9600);
  delay(1000);

  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    sendData();
  } else {
    Serial.println("WiFi disconnected");
  }

  delay(10000); // send every 10 seconds
}

void sendData() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  int moisture = 520;
  int temperature = 23;
  String pumpStatus = "OFF";

  String url = String(serverUrl);
  url += "?moisture=" + String(moisture);
  url += "&temperature=" + String(temperature);
  url += "&pumpStatus=" + pumpStatus;

  Serial.print("Sending to: ");
  Serial.println(url);

  http.begin(client, url);

  int httpCode = http.GET();

  Serial.print("HTTP response code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.println("Response:");
    Serial.println(response);
  } else {
    Serial.println("Failed to send data");
  }

  http.end();
}