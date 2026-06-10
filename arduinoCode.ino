<<<<<<< HEAD
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
=======
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

// Pumps through relay modules
#define WATER_PUMP_RELAY_PIN 7   // clean water -> plants
#define FILTER_PUMP_RELAY_PIN 8  // dirty water -> filter -> clean tank

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

// Battery divider:
// Battery + ---- 100k ---- A2 ---- 10k ---- GND
const float BATTERY_R1 = 100000.0;
const float BATTERY_R2 = 10000.0;
const float ARDUINO_REF_VOLTAGE = 5.0;

// =====================
// WATERING SETTINGS
// =====================
const int BASE_DRY_THRESHOLD = 30;
const int EMERGENCY_DRY_THRESHOLD = 20;

const unsigned long WATER_PUMP_TIME = 5000;    // clean -> plants
const unsigned long FILTER_PUMP_TIME = 7000;   // dirty -> filter -> clean
const unsigned long WATER_COOLDOWN = 90000;    // 90 seconds
const unsigned long DATA_INTERVAL = 5000;      // 5 seconds

const float BATTERY_CUTOFF_VOLTAGE = 13.0;     // 4S LiPo safety

// Most Grove relays are active HIGH.
// If your relay turns ON with LOW, swap these.
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

unsigned long lastWaterTime = 0;
unsigned long lastDataTime = 0;

// =====================
// FUNCTION DECLARATIONS
// =====================
SensorData readSensors();
int getEffectiveDryThreshold(SensorData data);
void handleButton();
void handleWateringLogic(SensorData data);
void runWaterPump(unsigned long runTime);
void runFilterPump(unsigned long runTime);
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

  data.effectiveDryThreshold = BASE_DRY_THRESHOLD;

  return data;
}

int getEffectiveDryThreshold(SensorData data) {
  int threshold = BASE_DRY_THRESHOLD;

  if (data.airTempC > 28) {
    threshold += 5;
  } else if (data.airTempC < 10) {
    threshold -= 3;
  }

  if (data.humidity < 35) {
    threshold += 3;
  } else if (data.humidity > 80) {
    threshold -= 3;
  }

  if (data.lightLux > 15000) {
    threshold += 2;
  }

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
    Serial.println(F("Watering needed. Running clean water pump."));

    // Pump 1: clean reservoir -> plants
    runWaterPump(WATER_PUMP_TIME);

    // Short pause before moving dirty/drain water through filter
    delay(500);

    // Pump 2: dirty reservoir -> filter -> clean reservoir
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

  Serial.print(F("Threshold: "));
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
>>>>>>> a09d0c6171b666d032e85601f589bd976e7e6c30
