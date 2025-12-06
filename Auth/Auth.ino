#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>

// ====== WiFi & MQTT Config ======
const char* WIFI_SSID     = "Orang Cerdas";
const char* WIFI_PASSWORD = "12345678e";

const char* MQTT_HOST = "broker.hivemq.com"; // or your local broker IP
const uint16_t MQTT_PORT = 1883;
static char MQTT_CLIENT_ID[32]; // will fill from MAC
const char* TOPIC_AUTH_RESULT = "dualguard/lock/cmd"; // Lock side subscribes here
const char* TOPIC_HEARTBEAT   = "dualguard/auth/heartbeat";
const char* TOPIC_LWT         = "dualguard/auth/lwt";

// ====== RFID Config (MFRC522 over SPI) ======
// Typical: SDA/SS -> GPIO 5, SCK -> 18, MOSI -> 23, MISO -> 19, RST -> 22
static const int RFID_SS_PIN  = 5;   // SDA / SS
static const int RFID_RST_PIN = 22;  // RST

MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

// ====== RTOS / queue config ======
#define UID_MAX_LEN 32
typedef struct {
  char uid[UID_MAX_LEN]; // null-terminated uppercase hex
} uid_item_t;

QueueHandle_t qUIDs = nullptr;
TaskHandle_t taskRFIDHandle = nullptr;
TaskHandle_t taskAuthHandle = nullptr;

// ====== Known Authorized UIDs ======
// Keep uppercase, no spaces, length variable
const char AUTHORIZED_UIDS[][UID_MAX_LEN] = {
  "DEADBEEF",
  "A1B2C3D4",
  "8A06358F",
};
const size_t AUTHORIZED_COUNT = sizeof(AUTHORIZED_UIDS) / sizeof(AUTHORIZED_UIDS[0]);

// ====== WiFi/MQTT ======
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// forward
bool mqttReconnect();

// Convert uid to uppercase hex string without dynamic String
void uidToHexBuf(const MFRC522::Uid& uid, char *outBuf, size_t outLen) {
  if (outLen == 0) return;
  size_t pos = 0;
  for (byte i = 0; i < uid.size && (pos + 2) < outLen; i++) {
    uint8_t b = uid.uidByte[i];
    uint8_t hi = (b >> 4) & 0x0F;
    uint8_t lo = b & 0x0F;
    outBuf[pos++] = (hi < 10) ? ('0' + hi) : ('A' + (hi - 10));
    outBuf[pos++] = (lo < 10) ? ('0' + lo) : ('A' + (lo - 10));
  }
  outBuf[pos] = '\0';
}

// Case-insensitive authorized check but both sides stored uppercase
bool isAuthorizedBuf(const char *uidHex) {
  for (size_t i = 0; i < AUTHORIZED_COUNT; i++) {
    if (strcasecmp(uidHex, AUTHORIZED_UIDS[i]) == 0) return true;
  }
  return false;
}

void publishLockCommand(bool open, const char *uidHex) {
  // Payload: {"cmd":"open"|"deny","uid":"..."}
  char payload[128];
  snprintf(payload, sizeof(payload), "{\"cmd\":\"%s\",\"uid\":\"%s\"}", open ? "open" : "deny", uidHex);
  if (mqtt.connected()) {
    mqtt.publish(TOPIC_AUTH_RESULT, payload);
    Serial.printf("Published to %s: %s\n", TOPIC_AUTH_RESULT, payload);
  } else {
    Serial.println("Cannot publish: MQTT not connected");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Placeholder: if you subscribe later, handle incoming messages here.
  Serial.printf("MQTT msg on %s (len=%u)\n", topic, length);
}

// ====== WiFi / MQTT connect helpers ======
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
  mqtt.setCallback(mqttCallback);

  // LWT
  mqtt.connect(MQTT_CLIENT_ID, nullptr, nullptr, TOPIC_LWT, 0, true, "offline");

  unsigned long start = millis();
  unsigned long backoff = 500;
  unsigned long maxBackoff = 5000;
  while (!mqtt.connected() && (millis() - start) < 15000) {
    if (mqtt.connect(MQTT_CLIENT_ID)) {
      // subscribe here if needed
      mqtt.publish(TOPIC_LWT, "online", true);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(backoff));
    backoff = (backoff * 2 > maxBackoff) ? maxBackoff : backoff * 2;
  }
  return mqtt.connected();
}

// robust mqtt reconnect routine used in task
bool mqttReconnect() {
  if (mqtt.connected()) return true;
  if (!connectMQTTWithBackoff()) {
    Serial.println("MQTT reconnect failed");
    return false;
  }
  Serial.println("MQTT connected");
  return true;
}

// ====== Tasks ======
void taskRFID(void* pv) {
  (void)pv;
  // Initialize SPI and RFID
  SPI.begin(); // use default pins
  rfid.PCD_Init();
  Serial.println("RFID task started");
  uid_item_t item;
  for (;;) {
    if (!rfid.PICC_IsNewCardPresent()) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (!rfid.PICC_ReadCardSerial()) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    uidToHexBuf(rfid.uid, item.uid, sizeof(item.uid));
    // send to queue, wait a little
    if (xQueueSend(qUIDs, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.println("Queue full: UID dropped");
    } else {
      Serial.printf("Scanned UID: %s\n", item.uid);
    }
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    // small debounce
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

void taskAuthAndMQTT(void* pv) {
  (void)pv;
  uid_item_t item;
  unsigned long lastBeat = 0;
  unsigned long beatInterval = 5000;
  for (;;) {
    // ensure WiFi
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi not connected, attempting reconnect");
      if (!connectWiFiWithTimeout(10000)) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      } else {
        Serial.println("WiFi reconnected");
      }
    }
    // ensure MQTT
    if (!mqtt.connected()) {
      mqttReconnect();
    } else {
      mqtt.loop();
    }

    // Heartbeat
    if (millis() - lastBeat > beatInterval && mqtt.connected()) {
      lastBeat = millis();
      mqtt.publish(TOPIC_HEARTBEAT, MQTT_CLIENT_ID);
    }

    // Process queued UIDs
    if (xQueueReceive(qUIDs, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
      bool ok = isAuthorizedBuf(item.uid);
      if (mqtt.connected()) {
        publishLockCommand(ok, item.uid);
      } else {
        Serial.printf("Auth result for %s -> %s (MQTT offline)\n", item.uid, ok ? "open" : "deny");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("DualGuard Auth starting...");

  // Build unique MQTT client id from MAC
  uint64_t mac = ESP.getEfuseMac();
  uint32_t mac_hi = (uint32_t)(mac >> 32);
  uint32_t mac_lo = (uint32_t)(mac & 0xFFFFFFFF);
  snprintf(MQTT_CLIENT_ID, sizeof(MQTT_CLIENT_ID), "DualGuardAuth%08X%08X", mac_hi, mac_lo);

  // Create queue for UIDs
  qUIDs = xQueueCreate(8, sizeof(uid_item_t));
  if (qUIDs == nullptr) {
    Serial.println("Queue create failed");
    while (true) vTaskDelay(pdMS_TO_TICKS(1000)); // fatal
  }

  // Start WiFi
  if (!connectWiFiWithTimeout(20000)) {
    Serial.println("WiFi connect failed");
    // continue anyway - tasks will retry
  } else {
    Serial.println("WiFi connected");
  }

  // Try connect MQTT once (task will maintain)
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  if (!mqttReconnect()) {
    Serial.println("Initial MQTT connect failed, will retry in task");
  }

  // Create tasks pinned to different cores (optional)
  BaseType_t r1 = xTaskCreatePinnedToCore(taskRFID, "RFID", 4096, nullptr, 2, &taskRFIDHandle, 0);
  BaseType_t r2 = xTaskCreatePinnedToCore(taskAuthAndMQTT, "AuthMQTT", 8192, nullptr, 2, &taskAuthHandle, 1);
  if (r1 != pdPASS || r2 != pdPASS) {
    Serial.println("Task create failed");
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void loop() {
  // all work done in tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}
