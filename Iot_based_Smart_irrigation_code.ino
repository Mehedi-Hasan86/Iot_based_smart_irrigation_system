#define BLYNK_TEMPLATE_ID "owntemplate_id"
#define BLYNK_TEMPLATE_NAME "Smart Irrigation System"
#define BLYNK_AUTH_TOKEN "Use_your_own_auth_token"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>

#define SOIL_PIN 34
#define DHT_PIN 4
#define RELAY_PIN 5
#define LED_PIN 2
#define RESET_BTN 0

#define WATER_TIME 10000
#define READ_INTERVAL 5000

DHT dht(DHT_PIN, DHT22);
BlynkTimer timer;
WiFiManager wifiManager;
WebServer server(80);

float soil = 0, temp = 0, hum = 0;
bool pumping = false;
int waterings = 0;
int dryThreshold = 30;
unsigned long lastWater = 0;

float readSoil();
void readSensors();
void checkWatering();
void water();
void setupWeb();
void handleRoot();
void handleData();
void handleWater();

BLYNK_WRITE(V3) {
  int state = param.asInt();
  if (state == 1) {
    digitalWrite(RELAY_PIN, LOW);   // motor ON for active LOW relay
    Blynk.virtualWrite(V5, "Motor ON");
  } else {
    digitalWrite(RELAY_PIN, HIGH);  // motor OFF for active LOW relay
    Blynk.virtualWrite(V5, "Motor OFF");
  }
}

BLYNK_WRITE(V4) {
  dryThreshold = param.asInt();
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RESET_BTN, INPUT_PULLUP);

  digitalWrite(RELAY_PIN, HIGH);   // relay OFF at start for active LOW
  digitalWrite(LED_PIN, LOW);

  dht.begin();

  if (!wifiManager.autoConnect("SmartIrrigation", "12345678")) {
    ESP.restart();
  }

  setupWeb();

  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect(5000);

  timer.setInterval(READ_INTERVAL, readSensors);
}

void loop() {
  server.handleClient();
  Blynk.run();
  timer.run();

  static unsigned long btnPress = 0;
  if (digitalRead(RESET_BTN) == LOW) {
    if (btnPress == 0) btnPress = millis();
    if (millis() - btnPress > 5000) {
      wifiManager.resetSettings();
      ESP.restart();
    }
  } else {
    btnPress = 0;
  }
}

void readSensors() {
  soil = readSoil();
  temp = dht.readTemperature();
  hum = dht.readHumidity();

  if (isnan(temp)) temp = 25;
  if (isnan(hum)) hum = 60;

  Serial.printf("Soil: %.1f%% | Temp: %.1f°C | Hum: %.1f%%\n", soil, temp, hum);

  if (Blynk.connected()) {
    Blynk.virtualWrite(V0, soil);
    Blynk.virtualWrite(V1, temp);
    Blynk.virtualWrite(V2, hum);
    Blynk.virtualWrite(V5, pumping ? "Watering" : "Normal");
  }

  checkWatering();
}

void checkWatering() {
  if (soil < dryThreshold && !pumping && millis() - lastWater > 60000) {
    water();
  }
}

void water() {
  pumping = true;
  waterings++;
  lastWater = millis();

  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, HIGH);

  if (Blynk.connected()) {
    Blynk.virtualWrite(V5, "Watering");
    Blynk.logEvent("watering_alert", "Watering plant!");
  }

  unsigned long start = millis();
  while (millis() - start < WATER_TIME) {
    delay(100);
    server.handleClient();
    Blynk.run();
  }

  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(LED_PIN, LOW);
  pumping = false;

  if (Blynk.connected()) {
    Blynk.virtualWrite(V5, "Normal");
  }
}

float readSoil() {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(SOIL_PIN);
    delay(10);
  }
  float val = map(sum / 10, 4095, 1500, 0, 100);
  return constrain(val, 0, 100);
}

void setupWeb() {
  server.on("/", handleRoot);
  server.on("/api/data", handleData);
  server.on("/api/water", HTTP_POST, handleWater);
  server.begin();
}

void handleRoot() {
  server.send(200, "text/plain", "Smart Irrigation Running");
}

void handleData() {
  String json = "{\"soil\":" + String(soil) +
                ",\"temp\":" + String(temp) +
                ",\"hum\":" + String(hum) +
                ",\"waterings\":" + String(waterings) + "}";
  server.send(200, "application/json", json);
}

void handleWater() {
  if (!pumping) {
    water();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Already watering");
  }
}
