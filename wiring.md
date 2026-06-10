RAW BATTERY
Raw Battery + 
├── 5V UBEC IN+
├── 3.3V UBEC IN+
└── 100kΩ resistor → Arduino A2 battery divider

Raw Battery -
└── Ground Rail


5V POWER
5V UBEC OUT+
└── 5V Rail

5V UBEC OUT-
└── Ground Rail

5V Rail
├── Arduino 5V
├── Grove Base Shield 5V
├── Grove sensors through Grove ports
├── LCD through Grove I2C
├── Grove relays through D7/D8 ports
├── D7 Relay COM
└── D8 Relay COM


3.3V POWER
3.3V UBEC OUT+
├── ESP8266 3V3
└── microSD VCC

3.3V UBEC OUT-
└── Ground Rail


GROUND
Ground Rail
├── Raw Battery -
├── 5V UBEC OUT-
├── 3.3V UBEC OUT-
├── Arduino GND
├── Grove Base Shield GND
├── ESP8266 GND
├── microSD GND
├── D7 Pump -
├── D8 Pump -
└── 10kΩ resistor from battery divider


GROVE SENSOR PORTS ON ARDUINO BASE SHIELD
Grove Soil Moisture Sensor
└── Grove A0 port
    └── Arduino A0

Grove Button
└── Grove A1 port
    └── Arduino A1

Grove DHT11
└── Grove D2 port
    └── Arduino D2

Grove Soil Temperature Sensor
└── Grove D4 port
    └── Arduino D4

Grove BH1750 Light Sensor
└── Grove I2C port
    ├── Arduino A4 SDA
    └── Arduino A5 SCL

I2C LCD
└── Grove I2C port
    ├── Arduino A4 SDA
    └── Arduino A5 SCL


BATTERY CHECKER
Raw Battery +
└── 100kΩ resistor
    └── Arduino A2
        └── 10kΩ resistor
            └── Ground Rail


RELAY CONTROL PORTS
Grove Relay for D7 Pump
└── Grove D7 port
    └── Arduino D7

Grove Relay for D8 Pump
└── Grove D8 port
    └── Arduino D8


D7 PUMP POWER
5V Rail
└── D7 Relay COM
    └── D7 Relay NO
        └── D7 Pump +

D7 Pump -
└── Ground Rail


D8 PUMP POWER
5V Rail
└── D8 Relay COM
    └── D8 Relay NO
        └── D8 Pump +

D8 Pump -
└── Ground Rail


ARDUINO TO ESP SERIAL
Arduino D5
└── ESP8266 D2 / GPIO4

Arduino D6
└── 1kΩ resistor
    └── ESP8266 D1 / GPIO5
        └── 2kΩ resistor
            └── Ground Rail


ESP8266 MICROSD
ESP8266 D0 / GPIO16
└── microSD CS

ESP8266 D5 / GPIO14
└── microSD SCK

ESP8266 D6 / GPIO12
└── microSD MISO

ESP8266 D7 / GPIO13
└── microSD MOSI

3.3V Rail
└── microSD VCC

Ground Rail
└── microSD GND