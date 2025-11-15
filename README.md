🌾 Smart Fertilizer Uploader (NodeMCU + ThingSpeak + SMS + Gmail)

This IoT project automatically monitors leaf color, soil moisture, temperature, and humidity to generate crop-specific fertilizer and irrigation recommendations.
It sends data to ThingSpeak, and alerts via Twilio SMS and Gmail SMTP email.

A special rule is added:
🚨 If soil moisture = 0% → The system immediately sends an “Irrigate Immediately” alert.

⭐ Features

Real-time sensing (RGB, Moisture, Temperature, Humidity)

Leaf color classification (Green/Yellow)

Nitrogen recommendation engine

Automatic irrigation alerts

60-second aggregation window

ThingSpeak cloud upload (8 fields)

SMS alerts (Twilio)

Email alerts (Gmail SMTP)

EEPROM-saved crop selection

🪴 Hardware Used
Component	Purpose
NodeMCU ESP8266	Wi-Fi MCU
TCS3200	Leaf color sensor
DHT11	Temp & humidity
Soil Moisture Sensor	Soil water content
Jumper wires + breadboard	Setup
USB cable	Upload/Power
🔌 Wiring Connections
Module	NodeMCU Pin
TCS S0	D1
TCS S1	D2
TCS S2	D5
TCS S3	D6
TCS OUT	D3
DHT11 Data	D7
Soil Moisture	A0
📡 ThingSpeak Fields
Field	Meaning
1	Red (kHz)
2	Green (kHz)
3	Blue (kHz)
4	Clear (kHz)
5	Hue
6	Saturation
7	Leaf color name
8	Soil Moisture (%)
🌱 Supported Crops

Rice

Wheat

Maize

Cotton

Chilli

Tomato

Each crop has unique thresholds for:

Moisture

Temperature

Humidity

Nitrogen requirement

📲 Advisory Logic (Summary)
🌧 Moisture Based
Condition	Message
0% moisture	🚨 Irrigate Immediately!
Too dry	Irrigate, then apply N
Good moisture	Apply Urea now
Saturated	Hold — soil saturated
🍃 Color Based (Leaf Nitrogen Status)
Color	Interpretation
GREEN	Nitrogen sufficient — no fertilizer
YELLOW	Nitrogen deficiency — apply Urea
🌡 Weather Based
Condition	Result
Temp ≥ threshold	Avoid fertilizing
RH ≤ threshold	Avoid fertilizing
🧩 Required Libraries

Install via Arduino Library Manager:

ESP8266WiFi

WiFiClientSecure

ThingSpeak

DHT sensor library

EEPROM

⚙️ Configuration

Update these values in the .ino file:

const char* WIFI_SSID = "yourWiFi";
const char* WIFI_PASS = "yourPassword";

const char* GMAIL_USER = "your@gmail.com";
const char* GMAIL_APP_PASS = "your16charapppassword";

const char* TWILIO_ACCOUNT_SID = "ACxxxxxxxxxxxx";
const char* TWILIO_AUTH_TOKEN  = "xxxxxxxxxxxx";
const char* TWILIO_FROM_NUMBER = "+1xxxxxxxx";
const char* TWILIO_TO_NUMBER   = "+91xxxxxxxxxx";

unsigned long TS_CHANNEL = XXXXXXX;
const char* TS_WRITE_KEY = "XXXXXXXXXXXXXX";

📧 Example Alerts
SMS Example
Rice: Irrigate immediately; GREEN; M0%

Email Example
Irrigate immediately
Crop: Rice
No N needed.

Serial Output
Advisory: Irrigate immediately
ThingSpeak OK
SMS sent | MAIL sent

📊 Sample Serial Output
AVG kHz R=10.5 G=9.8 B=7.2 C=8.9 | hue=118° sat=0.52 → GREEN
ENV Moist=0% Temp=32.0C RH=58%
Advisory: Irrigate immediately
ThingSpeak OK
SMS sent | MAIL sent
Next 60s sampling window started...
