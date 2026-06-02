#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>

// =====================
// DATA STRUCT
// =====================
struct SensorData {
  int moistureRaw;
  int moisturePercent;
  float airTempC;
  float humidity;
  float soilTempC;
  float lightLux;
  float batteryVoltage;
};

// =====================
// PINS
// =====================
#define MOISTURE_PIN A0
#define BATTERY_PIN A2

#define DHT_PIN 2
#define DHT_TYPE DHT11

#define SOIL_TEMP_PIN 4

#define ESP_RX_PIN 5
#define ESP_TX_PIN 6

#define IRRIGATION_RELAY_PIN 7
#define RETURN_RELAY_PIN 8

#define BUTTON2_PIN A1

// =====================
// OBJECTS
// =====================
DHT dht(DHT_PIN, DHT_TYPE);
BH1750 lightMeter;
OneWire oneWire(SOIL_TEMP_PIN);
DallasTemperature soilTempSensor(&oneWire);
LiquidCrystal_I2C lcd(0x27, 16, 2);
SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN);

// =====================
// CALIBRATION
// =====================
const int DRY_RAW = 850;
const int WET_RAW = 350;

const float BATTERY_R1 = 100000.0;
const float BATTERY_R2 = 33000.0;
const float ARDUINO_REF_VOLTAGE = 5.0;

// =====================
// SETTINGS
// =====================
const int DRY_THRESHOLD = 35;

const unsigned long IRRIGATION_PUMP_TIME = 5000;
const unsigned long IRRIGATION_COOLDOWN = 90000;
const unsigned long DATA_INTERVAL = 5000;

bool autoMode = true;
bool irrigationPumpState = false;
bool returnPumpState = false;

unsigned long lastIrrigationTime = 0;
unsigned long lastDataTime = 0;

// Function declarations
SensorData readSensors();
void handleButton();
void runIrrigationPump(unsigned long runTime);
void printToSerial(SensorData data);
void updateLCD(SensorData data);
void sendToESP(SensorData data);

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);

  Wire.begin();

  dht.begin();
  lightMeter.begin();
  soilTempSensor.begin();

  lcd.init();
  lcd.backlight();

  pinMode(IRRIGATION_RELAY_PIN, OUTPUT);
  pinMode(RETURN_RELAY_PIN, OUTPUT);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  digitalWrite(IRRIGATION_RELAY_PIN, LOW);
  digitalWrite(RETURN_RELAY_PIN, LOW);

  lcd.setCursor(0, 0);
  lcd.print(F("Smart Garden"));
  lcd.setCursor(0, 1);
  lcd.print(F("Starting..."));

  Serial.println(F("Smart Garden Starting..."));

  delay(1500);
  lcd.clear();
}

void loop() {
  unsigned long now = millis();

  handleButton();

  SensorData data = readSensors();

  if (autoMode) {
    if (data.moisturePercent < DRY_THRESHOLD &&
        now - lastIrrigationTime > IRRIGATION_COOLDOWN) {
      runIrrigationPump(IRRIGATION_PUMP_TIME);
      lastIrrigationTime = millis();
    }
  }

  if (now - lastDataTime >= DATA_INTERVAL) {
    lastDataTime = now;

    data = readSensors();

    printToSerial(data);
    updateLCD(data);
    sendToESP(data);
  }
}

SensorData readSensors() {
  SensorData data;

  data.moistureRaw = analogRead(MOISTURE_PIN);
  data.moisturePercent = map(data.moistureRaw, DRY_RAW, WET_RAW, 0, 100);
  data.moisturePercent = constrain(data.moisturePercent, 0, 100);

  data.airTempC = dht.readTemperature();
  data.humidity = dht.readHumidity();

  if (isnan(data.airTempC)) data.airTempC = -999;
  if (isnan(data.humidity)) data.humidity = -999;

  soilTempSensor.requestTemperatures();
  data.soilTempC = soilTempSensor.getTempCByIndex(0);

  data.lightLux = lightMeter.readLightLevel();

  int batteryRaw = analogRead(BATTERY_PIN);
  float pinVoltage = batteryRaw * (ARDUINO_REF_VOLTAGE / 1023.0);
  data.batteryVoltage = pinVoltage * ((BATTERY_R1 + BATTERY_R2) / BATTERY_R2);

  return data;
}

void handleButton() {
  static bool lastButtonState = HIGH;
  bool buttonState = digitalRead(BUTTON2_PIN);

  if (lastButtonState == HIGH && buttonState == LOW) {
    autoMode = !autoMode;

    Serial.print(F("Auto mode: "));
    Serial.println(autoMode ? F("ON") : F("OFF"));

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Auto Mode:"));
    lcd.setCursor(0, 1);
    lcd.print(autoMode ? F("ON") : F("OFF"));

    delay(300);
  }

  lastButtonState = buttonState;
}

void runIrrigationPump(unsigned long runTime) {
  irrigationPumpState = true;
  digitalWrite(IRRIGATION_RELAY_PIN, HIGH);

  delay(runTime);

  digitalWrite(IRRIGATION_RELAY_PIN, LOW);
  irrigationPumpState = false;
}

void printToSerial(SensorData data) {
  Serial.println(F("====== SMART GARDEN DATA ======"));

  Serial.print(F("Moisture Raw: "));
  Serial.println(data.moistureRaw);

  Serial.print(F("Moisture: "));
  Serial.print(data.moisturePercent);
  Serial.println(F("%"));

  Serial.print(F("Air Temp: "));
  Serial.print(data.airTempC);
  Serial.println(F(" C"));

  Serial.print(F("Humidity: "));
  Serial.print(data.humidity);
  Serial.println(F("%"));

  Serial.print(F("Soil Temp: "));
  Serial.print(data.soilTempC);
  Serial.println(F(" C"));

  Serial.print(F("Light: "));
  Serial.print(data.lightLux);
  Serial.println(F(" lux"));

  Serial.print(F("Battery: "));
  Serial.print(data.batteryVoltage);
  Serial.println(F(" V"));

  Serial.print(F("Pump: "));
  Serial.println(irrigationPumpState ? F("ON") : F("OFF"));

  Serial.print(F("Auto Mode: "));
  Serial.println(autoMode ? F("ON") : F("OFF"));

  Serial.println(F("================================"));
}

void updateLCD(SensorData data) {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(F("M:"));
  lcd.print(data.moisturePercent);
  lcd.print(F("% T:"));
  lcd.print(data.airTempC, 1);

  lcd.setCursor(0, 1);
  lcd.print(F("H:"));
  lcd.print(data.humidity, 0);
  lcd.print(F("% B:"));
  lcd.print(data.batteryVoltage, 1);
}

void sendToESP(SensorData data) {
  espSerial.print(F("MOIST="));
  espSerial.print(data.moisturePercent);

  espSerial.print(F(",AIR="));
  espSerial.print(data.airTempC);

  espSerial.print(F(",HUM="));
  espSerial.print(data.humidity);

  espSerial.print(F(",SOIL="));
  espSerial.print(data.soilTempC);

  espSerial.print(F(",LIGHT="));
  espSerial.print(data.lightLux);

  espSerial.print(F(",PUMP="));
  espSerial.print(irrigationPumpState ? F("ON") : F("OFF"));

  espSerial.print(F(",BATT="));
  espSerial.println(data.batteryVoltage);
}