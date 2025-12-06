#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>

// ====== WiFi & MQTT Config ======
const char* WIFI_SSID     = "Wifiiii";
const char* WIFI_PASSWORD = "00111H4h4";

const char* MQTT_HOST = "broker.hivemq.com"; // or your local broker IP
const uint16_t MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "DualGuard-Auth-ESP32";
const char* TOPIC_AUTH_RESULT = "dualguard/lock/cmd"; // Lock side subscribes here
const char* TOPIC_HEARTBEAT   = "dualguard/auth/heartbeat";

// ====== RFID Config (MFRC522 over SPI) ======
// ESP32 default HSPI pins vary; adjust for your wiring.
// Typical: SDA/SS -> GPIO 5, SCK -> 18, MOSI -> 23, MISO -> 19, RST -> 22
static const int RFID_SS_PIN  = 5;   // SDA / SS
static const int RFID_RST_PIN = 22;  // RST

MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

// ====== RTOS Primitives ======
// Queue carries scanned UID strings to auth task
QueueHandle_t qUIDs;

// ====== Known Authorized UIDs ======
// Fill with your card UIDs (hex, no spaces). Example: "A1B2C3D4".
const char* AUTHORIZED_UIDS[] = {
  "DEADBEEF",
  "A1B2C3D4",
};
const size_t AUTHORIZED_COUNT = sizeof(AUTHORIZED_UIDS) / sizeof(AUTHORIZED_UIDS[0]);

// ====== WiFi/MQTT ======
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
  return mqtt.connected();
}

// ====== Helpers ======
String uidToHex(const MFRC522::Uid& uid) {
  char buf[3];
  String out;
  for (byte i = 0; i < uid.size; i++) {
    sprintf(buf, "%02X", uid.uidByte[i]);
    out += buf;
  }
  return out;
}

bool isAuthorized(const String& uidHex) {
  for (size_t i = 0; i < AUTHORIZED_COUNT; i++) {
    if (uidHex.equalsIgnoreCase(AUTHORIZED_UIDS[i])) return true;
  }
  return false;
}

void publishLockCommand(bool open, const String& uidHex) {
  // Payload: {"cmd":"open"|"deny","uid":"..."}
  String payload = String("{\"cmd\":\"") + (open ? "open" : "deny") + "\",\"uid\":\"" + uidHex + "\"}";
  mqtt.publish(TOPIC_AUTH_RESULT, payload.c_str());
}

// ====== Tasks ======
void taskRFID(void* pv) {
  (void)pv;
  SPI.begin();
  rfid.PCD_Init();
  for (;;) {
    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    String uidHex = uidToHex(rfid.uid);
    // Send to queue (copy String to char[16])
    char uidBuf[32];
    strncpy(uidBuf, uidHex.c_str(), sizeof(uidBuf));
    uidBuf[sizeof(uidBuf) - 1] = '\0';
    xQueueSend(qUIDs, uidBuf, pdMS_TO_TICKS(50));
    // Halt for next
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void taskAuthAndMQTT(void* pv) {
  (void)pv;
  char uidBuf[32];
  unsigned long lastBeat = 0;
  for (;;) {
    // Maintain MQTT connection
    if (WiFi.status() == WL_CONNECTED) {
      if (!mqtt.connected()) connectMQTT();
      else mqtt.loop();
    }
    // Heartbeat every 5s
    if (millis() - lastBeat > 5000 && mqtt.connected()) {
      lastBeat = millis();
      mqtt.publish(TOPIC_HEARTBEAT, "auth-online");
    }
    // Process queued UIDs
    if (xQueueReceive(qUIDs, uidBuf, pdMS_TO_TICKS(50)) == pdTRUE) {
      String uidHex(uidBuf);
      bool ok = isAuthorized(uidHex);
      if (mqtt.connected()) publishLockCommand(ok, uidHex);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("DualGuard Auth starting...");

  // Create queue for UIDs (each item is char[32])
  qUIDs = xQueueCreate(6, 32);
  if (qUIDs == nullptr) {
    Serial.println("Queue create failed");
  }

  // WiFi + MQTT
  if (!connectWiFi()) Serial.println("WiFi connect failed");
  if (!connectMQTT()) Serial.println("MQTT connect failed");

  // Create tasks
  xTaskCreatePinnedToCore(taskRFID, "RFID", 4096, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(taskAuthAndMQTT, "AuthMQTT", 4096, nullptr, 2, nullptr, 1);
}

void loop() {
  // Nothing: work is done in RTOS tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}

