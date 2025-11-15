/*****************************************************
 * Smart Fertilizer Uploader (ThingSpeak 7 fields + SMS + Gmail)
 * NodeMCU ESP8266 + TCS3200 + Soil (A0) + DHT11
 * ThingSpeak F1..F7: R, G, B, C, Hue, Sat, ColorName
 * SMS: Twilio (HTTPS), Email: Gmail SMTP (App Password)
 * Cadence: TS every 15 s, Alerts (SMS+Email) every 60 s
 * Serial: 115200
 *****************************************************/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ThingSpeak.h>
#include <DHT.h>
#include <ctype.h>
#include <math.h>

/* ============ Pins ============ */
#define TCS_S0  D1
#define TCS_S1  D2
#define TCS_S2  D5
#define TCS_S3  D6
#define TCS_OUT D3
#define DHT_PIN D7
#define DHT_TYPE DHT11
#define SOIL_A0 A0




/* ============ Sensor tuning ============ */
const bool     USE_ALT_FILTER_MAP = false;
const uint8_t  SAMPLES_PER_CH     = 6;
const uint32_t PULSE_TIMEOUT_US   = 40000;
const float    INTENSITY_MIN_KHZ  = 2.0;
const float    WHITE_MIN_KHZ      = 8.0;
const float    WHITE_SAT_MAX      = 0.10;
const float    MIN_SAT_FOR_COLOR  = 0.12;

// Hue bins
const int RED1_MIN=344, RED1_MAX=360;
const int RED2_MIN=0,   RED2_MAX=16;
const int YEL_MIN =16,  YEL_MAX =70;
const int GRN_MIN =70,  GRN_MAX =170;
const int CYA_MIN =170, CYA_MAX =200;
const int BLU_MIN =200, BLU_MAX =270;
const int MAG_MIN =270, MAG_MAX =344;

// Soil calibration & thresholds
const int   SOIL_RAW_DRY = 900;
const int   SOIL_RAW_WET = 300;
const float MOISTURE_GOOD_MIN = 0.35;
const float MOISTURE_GOOD_MAX = 0.80;
const float MOISTURE_TOO_DRY  = 0.35;
const float MOISTURE_SATURATED= 0.80;

// Weather stress thresholds
const float TEMP_HIGH_C   = 35.0;
const float HUMID_LOW_PCT = 30.0;

/* ============ Cadence ============ */
const unsigned long PUSH_INTERVAL_MS = 15000; // >=15s for ThingSpeak free plan
const unsigned long ALERT_INTERVAL_MS= 60000; // SMS + Email throttle
unsigned long lastPush   = 0;
unsigned long lastAlertAt= 0;

/* ============ Types / Globals ============ */
struct RGBC { float r,g,b,c,avg; };
struct Advisory { String title; String body; };

DHT dht(DHT_PIN, DHT_TYPE);

/* ============ Wi-Fi ============ */
void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.printf("\nWiFi OK. IP: %s\n", WiFi.localIP().toString().c_str());
}

/* ============ TCS3200 ============ */
void tcsBegin() {
  pinMode(TCS_S0, OUTPUT); pinMode(TCS_S1, OUTPUT);
  pinMode(TCS_S2, OUTPUT); pinMode(TCS_S3, OUTPUT);
  pinMode(TCS_OUT, INPUT);
  digitalWrite(TCS_S0, HIGH);
  digitalWrite(TCS_S1, HIGH);
}
float measureKHz(bool s2, bool s3) {
  digitalWrite(TCS_S2, s2 ? HIGH : LOW);
  digitalWrite(TCS_S3, s3 ? HIGH : LOW);
  delayMicroseconds(200);
  unsigned long p = pulseIn(TCS_OUT, LOW, PULSE_TIMEOUT_US);
  if (p == 0) return 0.0f;
  return (1000000.0f / (float)p) / 1000.0f;
}
RGBC readRGBC() {
  RGBC v{0,0,0,0,0};
  for (uint8_t i=0;i<SAMPLES_PER_CH;i++) {
    float r = USE_ALT_FILTER_MAP ? measureKHz(true,  false) : measureKHz(false, false);
    float b = USE_ALT_FILTER_MAP ? measureKHz(true,  true ) : measureKHz(false, true );
    float c = USE_ALT_FILTER_MAP ? measureKHz(false, false) : measureKHz(true,  false);
    float g = USE_ALT_FILTER_MAP ? measureKHz(false, true ) : measureKHz(true,  true );
    v.r+=r; v.g+=g; v.b+=b; v.c+=c;
  }
  v.r/=SAMPLES_PER_CH; v.g/=SAMPLES_PER_CH; v.b/=SAMPLES_PER_CH; v.c/=SAMPLES_PER_CH;
  v.avg = (v.r + v.g + v.b)/3.0f;
  return v;
}
void normalizeRGB(const RGBC& in, float& nr, float& ng, float& nb) {
  float denom = in.c;
  if (denom < 0.001f) {
    float sum = in.r + in.g + in.b;
    if (sum < 0.001f) { nr=ng=nb=0; return; }
    nr=in.r/sum; ng=in.g/sum; nb=in.b/sum;
  } else {
    nr=in.r/denom; ng=in.g/denom; nb=in.b/denom;
  }
  float m = max(nr, max(ng, nb));
  if (m > 0.0001f) { nr/=m; ng/=m; nb/=m; }
}
void rgbToHueSat(float nr, float ng, float nb, int& hueDeg, float& sat) {
  float maxv = max(nr, max(ng, nb));
  float minv = min(nr, min(ng, nb));
  float delta = maxv - minv;
  sat = (maxv > 0.0001f) ? (delta / maxv) : 0.0f;
  float h;
  if (delta < 0.0001f) h = 0.0f;
  else if (maxv == nr) h = 60.0f * fmod(((ng - nb) / delta), 6.0f);
  else if (maxv == ng) h = 60.0f * (((nb - nr) / delta) + 2.0f);
  else                 h = 60.0f * (((nr - ng) / delta) + 4.0f);
  if (h < 0) h += 360.0f;
  hueDeg = (int)(h + 0.5f);
}
const char* nameColor(int hueDeg, float sat, const RGBC& raw) {
  if (raw.avg < INTENSITY_MIN_KHZ && raw.c < INTENSITY_MIN_KHZ) return "BLACK/TOO DARK";
  if (raw.c  > WHITE_MIN_KHZ && sat < WHITE_SAT_MAX)            return "WHITE";
  if (sat < MIN_SAT_FOR_COLOR)                                  return "GRAY/LOW SAT";
  if ((hueDeg >= RED1_MIN && hueDeg <= RED1_MAX) || (hueDeg >= RED2_MIN && hueDeg <= RED2_MAX)) return "RED";
  if (hueDeg >= YEL_MIN && hueDeg <  YEL_MAX) return "YELLOW";
  if (hueDeg >= GRN_MIN && hueDeg <  GRN_MAX) return "GREEN";
  if (hueDeg >= CYA_MIN && hueDeg <  CYA_MAX) return "CYAN";
  if (hueDeg >= BLU_MIN && hueDeg <  BLU_MAX) return "BLUE";
  if (hueDeg >= MAG_MIN && hueDeg <  MAG_MAX) return "MAGENTA";
  return "UNKNOWN";
}

/* ============ Soil & Weather ============ */
float readMoisture01() {
  int raw = analogRead(SOIL_A0);
  int clamped = constrain(raw, SOIL_RAW_WET, SOIL_RAW_DRY);
  float m = 1.0f - (float)(clamped - SOIL_RAW_WET) / (float)(SOIL_RAW_DRY - SOIL_RAW_WET);
  return constrain(m, 0.0f, 1.0f);
}
void readWeather(float& tC, float& rh) {
  tC = dht.readTemperature();
  rh = dht.readHumidity();
  if (isnan(tC) || isnan(rh)) { tC = NAN; rh = NAN; }
}

/* ============ ThingSpeak (7 fields) ============ */
bool pushThingSpeak(float r, float g, float b, float c, int hue, float sat,
                    const String& name, const String& decisionTitle)
{
  ThingSpeak.setField(1, r);
  ThingSpeak.setField(2, g);
  ThingSpeak.setField(3, b);
  ThingSpeak.setField(4, c);
  ThingSpeak.setField(5, hue);
  ThingSpeak.setField(6, sat);
  ThingSpeak.setField(7, name);
  ThingSpeak.setStatus(decisionTitle);
  int code = ThingSpeak.writeFields(TS_CHANNEL, TS_WRITE_KEY);
  Serial.printf("ThingSpeak write: HTTP %d\n", code);
  return code == 200;
}

/* ============ Twilio SMS ============ */
String toBase64(const String& in) {
  static const char* b64="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out; int val=0, valb=-6;
  for (size_t i=0;i<(size_t)in.length();++i){
    uint8_t c=(uint8_t)in.charAt(i);
    val=(val<<8)+c; valb+=8;
    while(valb>=0){ out+=b64[(val>>valb)&0x3F]; valb-=6; }
  }
  if (valb>-6) out+=b64[((val<<8)>>(valb+8))&0x3F];
  while (out.length()%4) out+='=';
  return out;
}
bool sendTwilioSMS(const String& bodyText) {
  WiFiClientSecure client; client.setTimeout(15000); client.setInsecure(); // pin CA in prod
  if (!client.connect(TWILIO_HOST, TWILIO_PORT)) { Serial.println("Twilio connect failed"); return false; }
  String path = String("/2010-04-01/Accounts/") + TWILIO_ACCOUNT_SID + "/Messages.json";
  auto urlEncode=[](const String&s)->String{
    String o; const char*h="0123456789ABCDEF";
    for(size_t i=0;i<(size_t)s.length();i++){
      char c=s.charAt(i);
      if (isalnum((unsigned char)c)||c=='-'||c=='_'||c=='.'||c=='~') o+=c;
      else if(c==' ') o+='+';
      else { o+='%'; o+=h[(c>>4)&0xF]; o+=h[c&0xF]; }
    }
    return o;
  };
  String form = "To="+urlEncode(TWILIO_TO_NUMBER)+
                "&From="+urlEncode(TWILIO_FROM_NUMBER)+
                "&Body="+urlEncode(bodyText);
  String auth = toBase64(String(TWILIO_ACCOUNT_SID)+":"+TWILIO_AUTH_TOKEN);
  String req = String("POST ")+path+" HTTP/1.1\r\nHost: "+TWILIO_HOST+
               "\r\nAuthorization: Basic "+auth+
               "\r\nContent-Type: application/x-www-form-urlencoded"+
               "\r\nContent-Length: "+String(form.length())+
               "\r\nConnection: close\r\n\r\n"+form;
  client.print(req);
  int status=-1; bool got=false; String line;
  while (client.connected()||client.available()){
    line=client.readStringUntil('\n');
    if(!got&&line.startsWith("HTTP/1.1 ")){ status=line.substring(9,12).toInt(); got=true; }
    if(line=="\r") break;
  }
  client.readString();
  Serial.printf("Twilio HTTP %d\n", status);
  return (status>=200 && status<300);
}

/* ============ Gmail SMTP (App Password) ============ */
bool smtpExpect(WiFiClientSecure& c, const char* tag) {
  unsigned long t0 = millis(); String resp;
  while (millis() - t0 < 8000) {
    while (c.available()) {
      String line = c.readStringUntil('\n'); resp += line;
      if (line.length() >= 4 && line[3] == ' ') { Serial.printf("[SMTP %s] %s", tag, resp.c_str()); return true; }
    }
    delay(10);
  }
  Serial.printf("[SMTP %s] Timeout\n", tag); return false;
}
bool sendGmail(const String& subject, const String& body) {
  WiFiClientSecure client; client.setTimeout(15000); client.setInsecure(); // pin CA in prod
  if (!client.connect(GMAIL_SMTP_HOST, GMAIL_SMTP_PORT)) { Serial.println("SMTP connect failed"); return false; }
  if (!smtpExpect(client,"Banner")) return false;
  client.printf("EHLO esp8266.local\r\n"); if (!smtpExpect(client,"EHLO")) return false;
  client.printf("AUTH LOGIN\r\n");         if (!smtpExpect(client,"AUTH")) return false;
  client.printf("%s\r\n", toBase64(String(GMAIL_USER)).c_str());     if (!smtpExpect(client,"USER")) return false;
  client.printf("%s\r\n", toBase64(String(GMAIL_APP_PASS)).c_str()); if (!smtpExpect(client,"PASS")) return false;
  client.printf("MAIL FROM:<%s>\r\n", GMAIL_FROM); if (!smtpExpect(client,"MAILFROM")) return false;
  client.printf("RCPT TO:<%s>\r\n",   GMAIL_TO);   if (!smtpExpect(client,"RCPTTO")) return false;
  client.printf("DATA\r\n");                        if (!smtpExpect(client,"DATA")) return false;
  String msg; 
  msg += "From: ESP8266 Advisor <"+String(GMAIL_FROM)+">\r\n";
  msg += "To: <"+String(GMAIL_TO)+">\r\n";
  msg += "Subject: "+subject+"\r\n";
  msg += "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\n";
  msg += body + "\r\n.\r\n";
  client.print(msg);                                if (!smtpExpect(client,"DOT")) return false;
  client.print("QUIT\r\n");
  return true;
}

/* ============ Decision Logic (overrides + messages) ============ */
Advisory buildAdvisory(const String& colorName, int hue, float sat,
                       float moist01, float tC, float rh)
{
  bool isWhite        = (colorName == "WHITE");
  bool lowSatGray     = (sat < MIN_SAT_FOR_COLOR) || (colorName == "GRAY/LOW SAT");
  bool leafGreen      = (colorName == "GREEN");
  bool leafYellow     = (colorName == "YELLOW");
  bool stressWX       = (!isnan(tC) && tC >= TEMP_HIGH_C) || (!isnan(rh) && rh <= HUMID_LOW_PCT);
  bool moistTooDry    = (moist01 <  MOISTURE_TOO_DRY);
  bool moistGood      = (moist01 >= MOISTURE_GOOD_MIN && moist01 <= MOISTURE_GOOD_MAX);
  bool moistSaturated = (moist01 >  MOISTURE_SATURATED);

  if (stressWX) {
    return {"⚠️ Avoid fertilizing now — plant under stress",
            "High temperature or low humidity detected.\n"
            "Temp="+String(tC,1)+"°C, RH="+String(rh,0)+"%.\nHold fertilizer."};
  }
  if (moistSaturated) {
    return {"⏸️ Hold fertilizer — soil saturated",
            "Soil moisture is high ("+String(moist01*100,0)+"%). Risk of leaching. Wait/drain, then reassess."};
  }
  if (leafYellow) {
    if (moistTooDry) {
      return {"💧 Irrigate first, then apply fertilizer",
              "Leaf yellowish but soil is dry ("+String(moist01*100,0)+"%). Irrigate to field capacity, then apply urea."};
    } else if (moistGood) {
      return {"✅ Apply Urea now",
              "Leaf yellowish + soil moisture good + weather normal.\n"
              "Soil moisture="+String(moist01*100,0)+"%.\nApply recommended dose today."};
    } else {
      return {"ℹ️ Observation recorded",
              "Yellow leaf but moisture not ideal ("+String(moist01*100,0)+"%). Adjust irrigation, then apply urea."};
    }
  }
  if (leafGreen) {
    return {"🌿 No fertilizer needed",
            "Leaf green (sufficient nitrogen). Maintain irrigation and monitoring."};
  }
  if (isWhite || lowSatGray) {
    return {"ℹ️ Observation recorded",
            "Color="+colorName+" (hue="+String(hue)+", sat="+String(sat,2)+"). "
            "Moisture="+String(moist01*100,0)+"% Temp="+String(tC,1)+"°C RH="+String(rh,0)+"%."};
  }
  return {"ℹ️ Observation recorded",
          "Color="+colorName+" (hue="+String(hue)+", sat="+String(sat,2)+"). "
          "Moisture="+String(moist01*100,0)+"% Temp="+String(tC,1)+"°C RH="+String(rh,0)+"%."};
}

/* ============ Setup / Loop ============ */
void setup() {
  Serial.begin(115200); delay(300);
  Serial.println("\n=== Smart Fertilizer Uploader — TS + SMS + Gmail ===");
  tcsBegin(); dht.begin(); ensureWiFi(); ThingSpeak.begin(tsClient);
}

void loop() {
  ensureWiFi();

  RGBC v = readRGBC();
  float nr, ng, nb; normalizeRGB(v, nr, ng, nb);
  int hue; float sat; rgbToHueSat(nr, ng, nb, hue, sat);
  String colorName = String(nameColor(hue, sat, v));

  Serial.println("------------------------------------------------");
  Serial.printf("RAW kHz  R=%.2f G=%.2f B=%.2f C=%.2f AVG=%.2f\n", v.r,v.g,v.b,v.c,v.avg);
  Serial.printf("NORM     r=%.2f g=%.2f b=%.2f | hue=%3d° sat=%.2f\n", nr,ng,nb,hue,sat);
  Serial.printf("Detected: %s\n", colorName.c_str());

  unsigned long now = millis();
  if (now - lastPush >= PUSH_INTERVAL_MS) {
    lastPush = now;

    float moist01 = readMoisture01();
    float tC, rh; readWeather(tC, rh);

    Advisory adv = buildAdvisory(colorName, hue, sat, moist01, tC, rh);

    bool tsOK = pushThingSpeak(v.r, v.g, v.b, v.c, hue, sat, colorName, adv.title);
    Serial.printf("ThingSpeak %s\n", tsOK ? "OK" : "FAIL");

    if (now - lastAlertAt >= ALERT_INTERVAL_MS) {
      String body = adv.title + "\n\n" +
                    "Leaf: " + colorName + " (hue=" + String(hue) + "°, sat=" + String(sat,2) + ")\n" +
                    "Soil moisture: " + String(moist01*100,0) + "%\n" +
                    "Temp: " + String(tC,1) + "°C, RH: " + String(rh,0) + "%\n";

      bool smsOK  = sendTwilioSMS(body);
      bool mailOK = sendGmail("Fertilizer Advisory", body);
      Serial.printf("SMS %s | MAIL %s\n", smsOK ? "sent" : "failed", mailOK ? "sent" : "failed");

      lastAlertAt = now;
    }
  }

  delay(200);
}
