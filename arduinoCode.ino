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
  bool validAir;
  bool validHumidity;
  bool validSoilTemp;
  bool validLight;
};

// =====================
// PINS
// =====================
#define MOISTURE_PIN A0
#define BATTERY_PIN A2

#define DHT_PIN 2
#define DHT_TYPE DHT11

#define SOIL_TEMP_PIN 4

// Arduino pin 5 receives from ESP TX.
// Arduino pin 6 transmits to ESP RX.
#define ESP_SERIAL_RX_PIN 5
#define ESP_SERIAL_TX_PIN 6

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
SoftwareSerial espSerial(ESP_SERIAL_RX_PIN, ESP_SERIAL_TX_PIN);

// =====================
// CALIBRATION
// =====================
const int DRY_RAW = 850;
const int WET_RAW = 350;

const float BATTERY_R1 = 100000.0;
const float BATTERY_R2 = 10000.0;
const float ARDUINO_REF_VOLTAGE = 5.0;

// Set to LOW if your relay module turns on when the input pin is LOW.
const int RELAY_ON = HIGH;
const int RELAY_OFF = LOW;

// =====================
// SETTINGS
// =====================
const int DRY_THRESHOLD = 35;

const unsigned long IRRIGATION_PUMP_TIME = 5000;
const unsigned long IRRIGATION_COOLDOWN = 90000;
const unsigned long DATA_INTERVAL = 5000;
const unsigned long LCD_INTERVAL = 1000;
const unsigned long BUTTON_DEBOUNCE = 250;

bool autoMode = true;
bool irrigationPumpState = false;
bool returnPumpState = false;

unsigned long pumpStopTime = 0;
unsigned long lastIrrigationTime = 0;
unsigned long lastDataTime = 0;
unsigned long lastLCDTime = 0;
unsigned long lastButtonPressTime = 0;

SensorData latestData;
bool hasSensorData = false;

// Function declarations
SensorData readSensors();
void handleButton();
void handleIrrigation(SensorData data);
void startIrrigationPump(unsigned long runTime);
void updatePump();
void setRelay(int pin, bool enabled);
void printToSerial(SensorData data);
void updateLCD(SensorData data);
void sendToESP(SensorData data);
void printFloatOrError(Print &out, float value, bool valid, int decimals);

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

  setRelay(IRRIGATION_RELAY_PIN, false);
  setRelay(RETURN_RELAY_PIN, false);

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
  updatePump();

  if (now - lastDataTime >= DATA_INTERVAL || !hasSensorData) {
    lastDataTime = now;
    latestData = readSensors();
    hasSensorData = true;

    handleIrrigation(latestData);
    printToSerial(latestData);
    sendToESP(latestData);
  }

  if (hasSensorData && now - lastLCDTime >= LCD_INTERVAL) {
    lastLCDTime = now;
    updateLCD(latestData);
  }
}

SensorData readSensors() {
  SensorData data;

  data.moistureRaw = analogRead(MOISTURE_PIN);
  data.moisturePercent = map(data.moistureRaw, DRY_RAW, WET_RAW, 0, 100);
  data.moisturePercent = constrain(data.moisturePercent, 0, 100);

  data.airTempC = dht.readTemperature();
  data.humidity = dht.readHumidity();
  data.validAir = !isnan(data.airTempC);
  data.validHumidity = !isnan(data.humidity);

  soilTempSensor.requestTemperatures();
  data.soilTempC = soilTempSensor.getTempCByIndex(0);
  data.validSoilTemp = data.soilTempC != DEVICE_DISCONNECTED_C;

  data.lightLux = lightMeter.readLightLevel();
  data.validLight = data.lightLux >= 0;

  int batteryRaw = analogRead(BATTERY_PIN);
  float pinVoltage = batteryRaw * (ARDUINO_REF_VOLTAGE / 1023.0);
  data.batteryVoltage = pinVoltage * ((BATTERY_R1 + BATTERY_R2) / BATTERY_R2);

  return data;
}

void handleButton() {
  static bool lastButtonState = HIGH;
  bool buttonState = digitalRead(BUTTON2_PIN);
  unsigned long now = millis();

  if (lastButtonState == HIGH &&
      buttonState == LOW &&
      now - lastButtonPressTime >= BUTTON_DEBOUNCE) {
    lastButtonPressTime = now;
    autoMode = !autoMode;

    Serial.print(F("Auto mode: "));
    Serial.println(autoMode ? F("ON") : F("OFF"));

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Auto Mode:"));
    lcd.setCursor(0, 1);
    lcd.print(autoMode ? F("ON") : F("OFF"));
  }

  lastButtonState = buttonState;
}

void handleIrrigation(SensorData data) {
  unsigned long now = millis();

  if (!autoMode || irrigationPumpState) {
    return;
  }

  if (data.moisturePercent < DRY_THRESHOLD &&
      now - lastIrrigationTime >= IRRIGATION_COOLDOWN) {
    startIrrigationPump(IRRIGATION_PUMP_TIME);
    lastIrrigationTime = now;
  }
}

void startIrrigationPump(unsigned long runTime) {
  irrigationPumpState = true;
  pumpStopTime = millis() + runTime;
  setRelay(IRRIGATION_RELAY_PIN, true);

  Serial.println(F("Irrigation pump ON"));
}

void updatePump() {
  if (irrigationPumpState && (long)(millis() - pumpStopTime) >= 0) {
    setRelay(IRRIGATION_RELAY_PIN, false);
    irrigationPumpState = false;
    Serial.println(F("Irrigation pump OFF"));
  }
}

void setRelay(int pin, bool enabled) {
  digitalWrite(pin, enabled ? RELAY_ON : RELAY_OFF);
}

void printToSerial(SensorData data) {
  Serial.println(F("====== SMART GARDEN DATA ======"));

  Serial.print(F("Moisture Raw: "));
  Serial.println(data.moistureRaw);

  Serial.print(F("Moisture: "));
  Serial.print(data.moisturePercent);
  Serial.println(F("%"));

  Serial.print(F("Air Temp: "));
  printFloatOrError(Serial, data.airTempC, data.validAir, 1);
  Serial.println(F(" C"));

  Serial.print(F("Humidity: "));
  printFloatOrError(Serial, data.humidity, data.validHumidity, 0);
  Serial.println(F("%"));

  Serial.print(F("Soil Temp: "));
  printFloatOrError(Serial, data.soilTempC, data.validSoilTemp, 1);
  Serial.println(F(" C"));

  Serial.print(F("Light: "));
  printFloatOrError(Serial, data.lightLux, data.validLight, 1);
  Serial.println(F(" lux"));

  Serial.print(F("Battery: "));
  Serial.print(data.batteryVoltage, 2);
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
  lcd.print(F("% A:"));
  lcd.print(autoMode ? F("ON") : F("OFF"));

  lcd.setCursor(0, 1);
  lcd.print(F("T:"));
  if (data.validAir) {
    lcd.print(data.airTempC, 1);
  } else {
    lcd.print(F("--"));
  }
  lcd.print(F(" P:"));
  lcd.print(irrigationPumpState ? F("ON") : F("OFF"));
}

void sendToESP(SensorData data) {
  espSerial.print(F("MOIST="));
  espSerial.print(data.moisturePercent);

  espSerial.print(F(",AIR="));
  printFloatOrError(espSerial, data.airTempC, data.validAir, 1);

  espSerial.print(F(",HUM="));
  printFloatOrError(espSerial, data.humidity, data.validHumidity, 0);

  espSerial.print(F(",SOIL="));
  printFloatOrError(espSerial, data.soilTempC, data.validSoilTemp, 1);

  espSerial.print(F(",LIGHT="));
  printFloatOrError(espSerial, data.lightLux, data.validLight, 1);

  espSerial.print(F(",PUMP="));
  espSerial.print(irrigationPumpState ? F("ON") : F("OFF"));

  espSerial.print(F(",AUTO="));
  espSerial.print(autoMode ? F("ON") : F("OFF"));

  espSerial.print(F(",BATT="));
  espSerial.println(data.batteryVoltage, 2);
}

void printFloatOrError(Print &out, float value, bool valid, int decimals) {
  if (valid) {
    out.print(value, decimals);
  } else {
    out.print(F("ERR"));
  }
}
