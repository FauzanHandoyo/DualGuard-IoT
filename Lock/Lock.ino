#define BLYNK_TEMPLATE_ID "TMPL6Ng3uuiOT"
#define BLYNK_TEMPLATE_NAME "Finpro"
#define BLYNK_AUTH_TOKEN "4zoswiTlfsnnoq_GL2Flq5zBr1znboy1"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <PubSubClient.h>

// WiFi & MQTT
const char* WIFI_SSID     = "Orang Cerdas";
const char* WIFI_PASSWORD = "12345678e";
const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
static char MQTT_CLIENT_ID[32];
const char* TOPIC_LOCK_CMD = "dualguard/lock/cmd";
const char* TOPIC_LWT      = "dualguard/lock/lwt";
const char* TOPIC_HEARTBEAT = "dualguard/lock/heartbeat";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// GPIO
static const int LOCK_LED_PIN = 27; // LED pengganti solenoid
static const int BUZZER_PIN = 21;   // buzzer aktif HIGH

// RTOS
QueueHandle_t qCmds = nullptr;
TaskHandle_t taskMQTTHandle = nullptr;
TaskHandle_t taskLockHandle = nullptr;

// command type
typedef enum { CMD_NONE = 0, CMD_OPEN, CMD_DENY } cmd_t;
typedef struct { cmd_t cmd; } cmd_item_t;

// Blynk virtual pins
#define BLYNK_V_SYSSTAT  V5
#define BLYNK_V_HEARTBEAT V6
#define BLYNK_V_LOCKCTRL V7

void makeClientIdFromMac(char *out, size_t outLen) {
  uint64_t mac = ESP.getEfuseMac();
  uint32_t hi = (uint32_t)(mac >> 32);
  uint32_t lo = (uint32_t)(mac & 0xFFFFFFFF);
  snprintf(out, outLen, "DualGuardLock%08X%08X", hi, lo);
}

void beepFailed() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(120));
    digitalWrite(BUZZER_PIN, LOW);
    vTaskDelay(pdMS_TO_TICKS(80));
  }
}

void beepSuccess() {
  // single short beep for success
  digitalWrite(BUZZER_PIN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(120));
  digitalWrite(BUZZER_PIN, LOW);
}

// MQTT callback
void onMessage(char* topic, byte* payload, unsigned int length) {
  (void)topic;
  const size_t BUF_SZ = 128;
  char buf[BUF_SZ];
  size_t copyLen = (length < (BUF_SZ - 1)) ? length : (BUF_SZ - 1);
  memcpy(buf, payload, copyLen);
  buf[copyLen] = '\0';

  cmd_item_t item; item.cmd = CMD_NONE;
  if (strstr(buf, "\"cmd\":\"open\"") != NULL) item.cmd = CMD_OPEN;
  else if (strstr(buf, "\"cmd\":\"deny\"") != NULL) item.cmd = CMD_DENY;
  else return;

  if (qCmds) {
    if (xQueueSend(qCmds, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.println("Queue full - command dropped");
    } else {
      Serial.printf("Cmd queued: %s\n", item.cmd == CMD_OPEN ? "OPEN" : "DENY");
    }
  }
}

bool connectWiFiWithTimeout(unsigned long timeoutMs = 20000) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  return WiFi.status() == WL_CONNECTED;
}

bool connectMQTTWithBackoff() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  unsigned long start = millis();
  unsigned long backoff = 500;
  const unsigned long maxBackoff = 4000;
  while (!mqtt.connected() && (millis() - start) < 15000) {
    if (mqtt.connect(MQTT_CLIENT_ID, TOPIC_LWT, 1, true, "offline")) {
      mqtt.subscribe(TOPIC_LOCK_CMD);
      mqtt.publish(TOPIC_LWT, "online", true);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(backoff));
    backoff = (backoff * 2 > maxBackoff) ? maxBackoff : backoff * 2;
  }
  return mqtt.connected();
}

void taskMQTT(void* pv) {
  (void)pv;
  unsigned long lastBeat = 0;
  unsigned long beatInterval = 5000;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi down, attempting reconnect...");
      if (!connectWiFiWithTimeout(10000)) { Serial.println("WiFi reconnect failed"); vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
      else Serial.println("WiFi reconnected");
    }
    if (!mqtt.connected()) {
      Serial.println("MQTT disconnected, attempting reconnect...");
      if (!connectMQTTWithBackoff()) { Serial.println("MQTT reconnect failed"); vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
      else Serial.println("MQTT reconnected");
    } else mqtt.loop();

    Blynk.run();

    if (millis() - lastBeat > beatInterval) {
      lastBeat = millis();
      String sys = String("WiFi:") + (WiFi.status() == WL_CONNECTED ? "OK" : "DOWN") + " MQTT:" + (mqtt.connected() ? "OK" : "DOWN");
      Blynk.virtualWrite(BLYNK_V_SYSSTAT, sys);
      Blynk.virtualWrite(BLYNK_V_HEARTBEAT, MQTT_CLIENT_ID);
      if (mqtt.connected()) mqtt.publish(TOPIC_HEARTBEAT, MQTT_CLIENT_ID);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void taskLock(void* pv) {
  (void)pv;
  cmd_item_t item;
  for (;;) {
    if (xQueueReceive(qCmds, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (item.cmd == CMD_OPEN) {
        Serial.println("Lock: OPEN -> LED HIGH for 10s");
        digitalWrite(LOCK_LED_PIN, HIGH);
        Blynk.virtualWrite(BLYNK_V_SYSSTAT, "LOCK:OPEN");
        beepSuccess();
        vTaskDelay(pdMS_TO_TICKS(10000));
        digitalWrite(LOCK_LED_PIN, LOW);
        Serial.println("Lock: CLOSED -> LED LOW");
        Blynk.virtualWrite(BLYNK_V_SYSSTAT, "LOCK:CLOSED");
      } else if (item.cmd == CMD_DENY) {
        Serial.println("Lock: DENY -> beep buzzer");
        Blynk.virtualWrite(BLYNK_V_SYSSTAT, "LOCK:DENIED");
        beepFailed();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

BLYNK_WRITE(BLYNK_V_LOCKCTRL) {
  int v = param.asInt();
  if (v == 1) {
    cmd_item_t it; it.cmd = CMD_OPEN;
    if (qCmds) {
      if (xQueueSend(qCmds, &it, pdMS_TO_TICKS(100)) == pdTRUE) {
        Serial.println("Manual lock open requested via Blynk");
        if (mqtt.connected()) mqtt.publish(TOPIC_HEARTBEAT, "manual_open_from_blynk");
      } else Serial.println("Queue full - manual open dropped");
    }
    Blynk.virtualWrite(BLYNK_V_LOCKCTRL, 0);
  }
}

void setup() {
  Serial.begin(115200);
  vTaskDelay(pdMS_TO_TICKS(100));

  pinMode(LOCK_LED_PIN, OUTPUT);
  digitalWrite(LOCK_LED_PIN, LOW);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  makeClientIdFromMac(MQTT_CLIENT_ID, sizeof(MQTT_CLIENT_ID));
  Serial.printf("MQTT client id: %s\n", MQTT_CLIENT_ID);

  qCmds = xQueueCreate(8, sizeof(cmd_item_t));
  if (!qCmds) { Serial.println("Queue creation FAILED"); while (true) vTaskDelay(pdMS_TO_TICKS(1000)); }

  connectWiFiWithTimeout(10000);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMessage);
  connectMQTTWithBackoff();

  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASSWORD);

  xTaskCreatePinnedToCore(taskMQTT, "MQTT", 4096, nullptr, 2, &taskMQTTHandle, 0);
  xTaskCreatePinnedToCore(taskLock,  "LOCK", 4096, nullptr, 2, &taskLockHandle, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
