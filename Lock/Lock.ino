#include <WiFi.h>
#include <PubSubClient.h>

// ========= WiFi & MQTT ==========
const char* WIFI_SSID     = "Orang Cerdas";
const char* WIFI_PASSWORD = "12345678e";

const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;

static char MQTT_CLIENT_ID[32]; // akan dibuat dari MAC
const char* TOPIC_LOCK_CMD = "dualguard/lock/cmd";
const char* TOPIC_LWT      = "dualguard/lock/lwt";

// ========= GPIO ==========
static const int LOCK_PIN = 27;

// ========= RTOS ==========
QueueHandle_t qCmds = nullptr;
TaskHandle_t taskMQTTHandle = nullptr;
TaskHandle_t taskLockHandle = nullptr;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// ===== command type =====
typedef enum {
  CMD_NONE = 0,
  CMD_OPEN,
  CMD_DENY
} cmd_t;

typedef struct {
  cmd_t cmd;
} cmd_item_t;

// ===== helper: make unique client id from MAC =====
void makeClientIdFromMac(char *out, size_t outLen) {
  uint64_t mac = ESP.getEfuseMac();
  uint32_t hi = (uint32_t)(mac >> 32);
  uint32_t lo = (uint32_t)(mac & 0xFFFFFFFF);
  snprintf(out, outLen, "DualGuardLock%08X%08X", hi, lo);
}

// ===== MQTT callback =====
// payload is not null-terminated; copy into a bounded buffer and check
void onMessage(char* topic, byte* payload, unsigned int length) {
  (void)topic;
  const size_t BUF_SZ = 128;
  char buf[BUF_SZ];
  size_t copyLen = (length < (BUF_SZ - 1)) ? length : (BUF_SZ - 1);
  memcpy(buf, payload, copyLen);
  buf[copyLen] = '\0'; // safe null terminate

  // simple search for "cmd":"open" or "cmd":"deny"
  cmd_item_t item;
  item.cmd = CMD_NONE;
  if (strstr(buf, "\"cmd\":\"open\"") != NULL) {
    item.cmd = CMD_OPEN;
  } else if (strstr(buf, "\"cmd\":\"deny\"") != NULL) {
    item.cmd = CMD_DENY;
  } else {
    // unknown message
    return;
  }

  if (qCmds) {
    if (xQueueSend(qCmds, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.println("Queue full - command dropped");
    } else {
      Serial.printf("Cmd queued: %s\n", item.cmd == CMD_OPEN ? "OPEN" : "DENY");
    }
  }
}

// ===== WiFi + MQTT helpers =====
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
    // set LWT on connect attempt
    // PubSubClient provides connect(clientId, willTopic, willQos, willRetain, willMessage)
    if (mqtt.connect(MQTT_CLIENT_ID, TOPIC_LWT, 1, true, "offline")) {
      // subscribe
      mqtt.subscribe(TOPIC_LOCK_CMD);
      // publish online LWT retained
      mqtt.publish(TOPIC_LWT, "online", true);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(backoff));
    backoff = (backoff * 2 > maxBackoff) ? maxBackoff : backoff * 2;
  }
  return mqtt.connected();
}

// ===== Tasks =====
void taskMQTT(void* pv) {
  (void)pv;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi down, attempting reconnect...");
      if (!connectWiFiWithTimeout(10000)) {
        Serial.println("WiFi reconnect failed");
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      } else {
        Serial.println("WiFi reconnected");
      }
    }
    if (!mqtt.connected()) {
      Serial.println("MQTT disconnected, attempting reconnect...");
      if (!connectMQTTWithBackoff()) {
        Serial.println("MQTT reconnect failed");
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      } else {
        Serial.println("MQTT reconnected");
      }
    } else {
      mqtt.loop();
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
        Serial.println("Lock: OPEN -> set GPIO HIGH for 10s");
        digitalWrite(LOCK_PIN, HIGH);
        // hold non-blocking for the lock duration inside this task
        vTaskDelay(pdMS_TO_TICKS(10000));
        digitalWrite(LOCK_PIN, LOW);
        Serial.println("Lock: CLOSED -> GPIO LOW");
      } else if (item.cmd == CMD_DENY) {
        Serial.println("Lock: DENY -> no action");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ===== Setup / Loop =====
void setup() {
  Serial.begin(115200);
  vTaskDelay(pdMS_TO_TICKS(100));

  pinMode(LOCK_PIN, OUTPUT);
  digitalWrite(LOCK_PIN, LOW);

  makeClientIdFromMac(MQTT_CLIENT_ID, sizeof(MQTT_CLIENT_ID));
  Serial.printf("MQTT client id: %s\n", MQTT_CLIENT_ID);

  qCmds = xQueueCreate(8, sizeof(cmd_item_t));
  if (!qCmds) {
    Serial.println("Queue creation FAILED");
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
  }

  // initial tries - tasks will maintain connections
  connectWiFiWithTimeout(10000);
  mqtt.setCallback(onMessage);
  connectMQTTWithBackoff();

  xTaskCreatePinnedToCore(taskMQTT, "MQTT", 4096, nullptr, 2, &taskMQTTHandle, 0);
  xTaskCreatePinnedToCore(taskLock,  "LOCK", 4096, nullptr, 2, &taskLockHandle, 1);
}

void loop() {
  // all work done in tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}
