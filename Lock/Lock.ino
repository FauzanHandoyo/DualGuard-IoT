#include <WiFi.h>
#include <PubSubClient.h>

// ====== WiFi & MQTT Config ======
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST = "broker.hivemq.com"; // or your local broker IP
const uint16_t MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "DualGuard-Lock-ESP32";
const char* TOPIC_LOCK_CMD = "dualguard/lock/cmd"; // Auth publishes here

// ====== Lock GPIO ======
// Drive a relay or servo trigger. Using GPIO 4 for example.
static const int LOCK_PIN = 4;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool connectMQTT() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  unsigned long start = millis();
  while (!mqtt.connected() && millis() - start < 10000) {
    mqtt.connect(MQTT_CLIENT_ID);
    delay(250);
  }
  if (mqtt.connected()) {
    mqtt.subscribe(TOPIC_LOCK_CMD);
  }
  return mqtt.connected();
}

void unlockPulse(uint32_t ms = 2000) {
  digitalWrite(LOCK_PIN, HIGH);
  delay(ms);
  digitalWrite(LOCK_PIN, LOW);
}

void onMessage(char* topic, byte* payload, unsigned int length) {
  // Expect JSON: {"cmd":"open"|"deny","uid":"..."}
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  if (msg.indexOf("\"cmd\":\"open\"") >= 0) {
    unlockPulse();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(LOCK_PIN, OUTPUT);
  digitalWrite(LOCK_PIN, LOW);

  if (!connectWiFi()) Serial.println("WiFi connect failed");
  mqtt.setCallback(onMessage);
  if (!connectMQTT()) Serial.println("MQTT connect failed");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) connectMQTT();
    else mqtt.loop();
  }
  delay(20);
}
