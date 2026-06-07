#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include <SD.h>

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
  int effectiveDryThreshold;
};

// =====================
// PINS
// =====================
#define MOISTURE_PIN A0
#define BUTTON2_PIN A1
#define BATTERY_PIN A2

#define DHT_PIN 2
#define DHT_TYPE DHT11

#define SOIL_TEMP_PIN 4

// Arduino SoftwareSerial to ESP8266
// Arduino D5 = RX from ESP
// Arduino D6 = TX to ESP
#define ESP_RX_PIN 5
#define ESP_TX_PIN 6

// Pumps through Grove relay modules
#define WATER_PUMP_RELAY_PIN 7   // clean reservoir -> plants
#define FILTER_PUMP_RELAY_PIN 8  // dirty reservoir -> filter -> clean reservoir

// microSD module
#define SD_CS_PIN 10

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
// MOISTURE CALIBRATION
// =====================
const int DRY_RAW = 850;
const int WET_RAW = 350;

// =====================
// BATTERY DIVIDER
// =====================
// Battery + ---- 100k ---- A2 ---- 10k ---- GND
const float BATTERY_R1 = 100000.0;
const float BATTERY_R2 = 10000.0;
const float ARDUINO_REF_VOLTAGE = 5.0;

// =====================
// WATERING SETTINGS
// =====================
const int BASE_DRY_THRESHOLD = 30;      // normal salal watering trigger
const int EMERGENCY_DRY_THRESHOLD = 20; // very dry soil
const int WATER_TARGET = 45;            // desired approximate soil moisture after watering

const unsigned long WATER_PUMP_TIME = 5000;      // 5 sec water pulse
const unsigned long FILTER_PUMP_TIME = 7000;     // 7 sec filter/return pulse
const unsigned long WATER_COOLDOWN = 90000;      // 90 sec between watering pulses
const unsigned long DATA_INTERVAL = 5000;        // read/send/log every 5 sec

// 4S LiPo safety
const float BATTERY_CUTOFF_VOLTAGE = 13.0;

// Relay type
// Most Grove relays are active HIGH.
// If your relays turn on when LOW, change these two values.
const int RELAY_ON = HIGH;
const int RELAY_OFF = LOW;

// =====================
// STATES
// =====================
bool autoMode = true;

bool waterPumpState = false;
bool filterPumpState = false;

bool lastWaterPumpRan = false;
bool lastFilterPumpRan = false;

bool sdReady = false;

unsigned long lastWaterTime = 0;
unsigned long lastDataTime = 0;

// =====================
// FUNCTION DECLARATIONS
// =====================
SensorData readSensors();
int getEffectiveDryThreshold(SensorData data);
void handleButton();
void runWaterPump(unsigned long runTime);
void runFilterPump(unsigned long runTime);
void handleWateringLogic(SensorData data);
void printToSerial(SensorData data);
void updateLCD(SensorData data);
void sendToESP(SensorData data);
void logToSD(SensorData data);
void setupSD();

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);

  Wire.begin();

  dht.begin();
  lightMeter.begin();
  soilTempSensor.begin();

  lcd.init();
  lcd.backlight();

  pinMode(WATER_PUMP_RELAY_PIN, OUTPUT);
  pinMode(FILTER_PUMP_RELAY_PIN, OUTPUT);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  digitalWrite(WATER_PUMP_RELAY_PIN, RELAY_OFF);
  digitalWrite(FILTER_PUMP_RELAY_PIN, RELAY_OFF);

  lcd.setCursor(0, 0);
  lcd.print(F("Smart Garden"));
  lcd.setCursor(0, 1);
  lcd.print(F("Starting..."));

  Serial.println(F("Smart Garden Starting..."));

  setupSD();

  delay(1500);
  lcd.clear();
}

void loop() {
  unsigned long now = millis();

  handleButton();

  if (now - lastDataTime >= DATA_INTERVAL) {
    lastDataTime = now;

    lastWaterPumpRan = false;
    lastFilterPumpRan = false;

    SensorData data = readSensors();
    data.effectiveDryThreshold = getEffectiveDryThreshold(data);

    if (autoMode) {
      handleWateringLogic(data);
    }

    printToSerial(data);
    updateLCD(data);
    sendToESP(data);
    logToSD(data);
  }
}

void setupSD() {
  if (SD.begin(SD_CS_PIN)) {
    sdReady = true;
    Serial.println(F("SD OK"));

    if (!SD.exists("garden.csv")) {
      File file = SD.open("garden.csv", FILE_WRITE);

      if (file) {
        file.println(F("time_ms,moisture_raw,moisture_percent,effective_threshold,air_temp_c,humidity_percent,soil_temp_c,light_lux,battery_v,water_pump_ran,filter_pump_ran,auto_mode"));
        file.close();
      }
    }
  } else {
    sdReady = false;
    Serial.println(F("SD failed/not connected"));
  }
}

SensorData readSensors() {
  SensorData data;

  // Soil moisture
  data.moistureRaw = analogRead(MOISTURE_PIN);
  data.moisturePercent = map(data.moistureRaw, DRY_RAW, WET_RAW, 0, 100);
  data.moisturePercent = constrain(data.moisturePercent, 0, 100);

  // Air temperature and humidity
  data.airTempC = dht.readTemperature();
  data.humidity = dht.readHumidity();

  if (isnan(data.airTempC)) data.airTempC = -999;
  if (isnan(data.humidity)) data.humidity = -999;

  // Soil temperature
  soilTempSensor.requestTemperatures();
  data.soilTempC = soilTempSensor.getTempCByIndex(0);

  // Light level
  data.lightLux = lightMeter.readLightLevel();

  // Battery voltage from A2
  int batteryRaw = analogRead(BATTERY_PIN);
  float pinVoltage = batteryRaw * (ARDUINO_REF_VOLTAGE / 1023.0);
  data.batteryVoltage = pinVoltage * ((BATTERY_R1 + BATTERY_R2) / BATTERY_R2);

  data.effectiveDryThreshold = BASE_DRY_THRESHOLD;

  return data;
}

int getEffectiveDryThreshold(SensorData data) {
  int threshold = BASE_DRY_THRESHOLD;

  // Hot air = water earlier
  if (data.airTempC > 28) {
    threshold += 5;
  } else if (data.airTempC < 10) {
    threshold -= 3;
  }

  // Low humidity = water earlier
  if (data.humidity < 35) {
    threshold += 3;
  } else if (data.humidity > 80) {
    threshold -= 3;
  }

  // Bright light = slightly earlier watering
  if (data.lightLux > 15000) {
    threshold += 2;
  }

  // Cold soil = avoid overwatering
  if (data.soilTempC < 8) {
    threshold -= 5;
  } else if (data.soilTempC > 25) {
    threshold += 3;
  }

  threshold = constrain(threshold, 20, 45);

  return threshold;
}

void handleWateringLogic(SensorData data) {
  unsigned long now = millis();

  bool batteryOK = data.batteryVoltage > BATTERY_CUTOFF_VOLTAGE;
  bool emergencyDry = data.moisturePercent < EMERGENCY_DRY_THRESHOLD;
  bool dryEnough = data.moisturePercent < data.effectiveDryThreshold;
  bool cooldownDone = now - lastWaterTime > WATER_COOLDOWN;

  if ((batteryOK || emergencyDry) && dryEnough && cooldownDone) {
    Serial.println(F("Watering needed. Running water pump."));

    // Pump 1: clean reservoir -> plants
    runWaterPump(WATER_PUMP_TIME);

    // Pump 2: dirty reservoir -> filter -> clean reservoir
    // This runs after watering to recycle/clean drainage water.
    delay(500);
    runFilterPump(FILTER_PUMP_TIME);

    lastWaterTime = millis();
  }
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

void runWaterPump(unsigned long runTime) {
  waterPumpState = true;
  lastWaterPumpRan = true;

  digitalWrite(WATER_PUMP_RELAY_PIN, RELAY_ON);
  delay(runTime);
  digitalWrite(WATER_PUMP_RELAY_PIN, RELAY_OFF);

  waterPumpState = false;
}

void runFilterPump(unsigned long runTime) {
  filterPumpState = true;
  lastFilterPumpRan = true;

  digitalWrite(FILTER_PUMP_RELAY_PIN, RELAY_ON);
  delay(runTime);
  digitalWrite(FILTER_PUMP_RELAY_PIN, RELAY_OFF);

  filterPumpState = false;
}

void printToSerial(SensorData data) {
  Serial.println(F("====== SMART GARDEN DATA ======"));

  Serial.print(F("Moisture Raw: "));
  Serial.println(data.moistureRaw);

  Serial.print(F("Moisture: "));
  Serial.print(data.moisturePercent);
  Serial.println(F("%"));

  Serial.print(F("Effective Dry Threshold: "));
  Serial.print(data.effectiveDryThreshold);
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

  Serial.print(F("Water Pump Ran: "));
  Serial.println(lastWaterPumpRan ? F("YES") : F("NO"));

  Serial.print(F("Filter Pump Ran: "));
  Serial.println(lastFilterPumpRan ? F("YES") : F("NO"));

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
  lcd.print(F("B:"));
  lcd.print(data.batteryVoltage, 1);
  lcd.print(F(" Th:"));
  lcd.print(data.effectiveDryThreshold);
}

void sendToESP(SensorData data) {
  espSerial.print(F("MOIST="));
  espSerial.print(data.moisturePercent);

  espSerial.print(F(",RAW="));
  espSerial.print(data.moistureRaw);

  espSerial.print(F(",THRESH="));
  espSerial.print(data.effectiveDryThreshold);

  espSerial.print(F(",AIR="));
  espSerial.print(data.airTempC);

  espSerial.print(F(",HUM="));
  espSerial.print(data.humidity);

  espSerial.print(F(",SOIL="));
  espSerial.print(data.soilTempC);

  espSerial.print(F(",LIGHT="));
  espSerial.print(data.lightLux);

  espSerial.print(F(",BATT="));
  espSerial.print(data.batteryVoltage);

  espSerial.print(F(",WATERPUMP="));
  espSerial.print(lastWaterPumpRan ? F("ON") : F("OFF"));

  espSerial.print(F(",FILTERPUMP="));
  espSerial.print(lastFilterPumpRan ? F("ON") : F("OFF"));

  espSerial.print(F(",AUTO="));
  espSerial.println(autoMode ? F("ON") : F("OFF"));
}

void logToSD(SensorData data) {
  if (!sdReady) return;

  File file = SD.open("garden.csv", FILE_WRITE);

  if (file) {
    file.print(millis());
    file.print(",");

    file.print(data.moistureRaw);
    file.print(",");

    file.print(data.moisturePercent);
    file.print(",");

    file.print(data.effectiveDryThreshold);
    file.print(",");

    file.print(data.airTempC);
    file.print(",");

    file.print(data.humidity);
    file.print(",");

    file.print(data.soilTempC);
    file.print(",");

    file.print(data.lightLux);
    file.print(",");

    file.print(data.batteryVoltage);
    file.print(",");

    file.print(lastWaterPumpRan ? F("YES") : F("NO"));
    file.print(",");

    file.print(lastFilterPumpRan ? F("YES") : F("NO"));
    file.print(",");

    file.println(autoMode ? F("ON") : F("OFF"));

    file.close();
  }
}
