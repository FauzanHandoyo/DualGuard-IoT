#define BLYNK_TEMPLATE_ID "TMPL6Ng3uuiOT"
#define BLYNK_TEMPLATE_NAME "Finpro"
#define BLYNK_AUTH_TOKEN "4zoswiTlfsnnoq_GL2Flq5zBr1znboy1"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include "DHT.h"
#include <SPI.h>
#include <MFRC522.h>
#include <PubSubClient.h>
#include <Preferences.h>

// WiFi & MQTT
const char* WIFI_SSID     = "Orang Cerdas";
const char* WIFI_PASSWORD = "12345678e";
const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
static char MQTT_CLIENT_ID[32];
const char* TOPIC_AUTH_RESULT = "dualguard/lock/cmd";
const char* TOPIC_HEARTBEAT   = "dualguard/auth/heartbeat";
const char* TOPIC_LWT         = "dualguard/auth/lwt";

// Blynk virtual pins
#define BLYNK_PIN_ADMIN   V1
#define BLYNK_V_SCANUID   V2
#define BLYNK_V_AUTHSTAT  V3
#define BLYNK_V_TOTALUID  V4
#define BLYNK_V_SYSSTAT   V5
#define BLYNK_V_HEARTBEAT V6
#define BLYNK_V_LOCKCTRL  V7
#define BLYNK_V_DELETEUID V8
#define BLYNK_V_REQLIST   V9
#define BLYNK_V_UIDLIST  V10
#define BLYNK_V_AUTO_OFF V11

// RFID pins
static const int RFID_SS_PIN  = 5;
static const int RFID_RST_PIN = 22;
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

// RTOS queue
#define UID_MAX_LEN 32
typedef struct { char uid[UID_MAX_LEN]; } uid_item_t;
QueueHandle_t qUIDs = nullptr;
TaskHandle_t taskRFIDHandle = nullptr;
TaskHandle_t taskAuthHandle = nullptr;

<<<<<<< HEAD
// Preferences storage
Preferences prefs;
#define PREF_KEY_AUTH "auth_list"
#define MAX_AUTH 32
char authList[MAX_AUTH][UID_MAX_LEN];
size_t authCount = 0;
=======
// ====== Known Authorized UIDs ======
// Keep uppercase, no spaces, length variable
const char AUTHORIZED_UIDS[][UID_MAX_LEN] = {
  "DEADBEEF",
  "A1B2C3D4",
  "8A06358F",
};
const size_t AUTHORIZED_COUNT = sizeof(AUTHORIZED_UIDS) / sizeof(AUTHORIZED_UIDS[0]);
>>>>>>> 5f6e09cc79452953b2729e47708cb32acecc984c

// State
volatile bool adminMode = false;
volatile bool adminAutoOff = true;

// WiFi/MQTT
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// Forward
bool mqttReconnect();

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

void saveAuthListToPrefs() {
  String s;
  for (size_t i = 0; i < authCount; i++) {
    if (i) s += ',';
    s += String(authList[i]);
  }
  prefs.putString(PREF_KEY_AUTH, s);
  Serial.printf("Saved %u authorized UIDs\n", (unsigned)authCount);
}

void loadAuthListFromPrefs() {
  authCount = 0;
  String s = prefs.getString(PREF_KEY_AUTH, "");
  if (s.length() == 0) {
    Serial.println("No stored authorized UIDs");
    return;
  }
  int start = 0;
  while (start < (int)s.length() && authCount < MAX_AUTH) {
    int comma = s.indexOf(',', start);
    String token;
    if (comma == -1) {
      token = s.substring(start);
      start = s.length();
    } else {
      token = s.substring(start, comma);
      start = comma + 1;
    }
    token.trim();
    token.toUpperCase();
    token.toCharArray(authList[authCount], UID_MAX_LEN);
    authCount++;
  }
  Serial.printf("Loaded %u authorized UIDs from prefs\n", (unsigned)authCount);
}

bool isAuthorizedBuf(const char *uidHex) {
  for (size_t i = 0; i < authCount; i++) {
    if (strcasecmp(uidHex, authList[i]) == 0) return true;
  }
  return false;
}

bool addAuthorizedUid(const char *uidHex) {
  if (isAuthorizedBuf(uidHex)) return false;
  if (authCount >= MAX_AUTH) return false;
  strncpy(authList[authCount], uidHex, UID_MAX_LEN-1);
  authList[authCount][UID_MAX_LEN-1] = '\0';
  authCount++;
  saveAuthListToPrefs();
  return true;
}

bool removeAuthorizedUid(const char *uidHex) {
  for (size_t i = 0; i < authCount; i++) {
    if (strcasecmp(uidHex, authList[i]) == 0) {
      for (size_t j = i; j + 1 < authCount; j++) {
        strncpy(authList[j], authList[j+1], UID_MAX_LEN);
      }
      authCount--;
      saveAuthListToPrefs();
      return true;
    }
  }
  return false;
}

String buildUidListString() {
  String out;
  for (size_t i = 0; i < authCount; i++) {
    out += String(authList[i]);
    if (i + 1 < authCount) out += "\n";
  }
  if (out.length() == 0) out = "(no UIDs)";
  return out;
}

void publishLockCommand(bool open, const char *uidHex) {
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
  Serial.printf("MQTT msg on %s (len=%u)\n", topic, length);
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
  mqtt.setCallback(mqttCallback);
  unsigned long start = millis();
  unsigned long backoff = 500;
  unsigned long maxBackoff = 5000;
  while (!mqtt.connected() && (millis() - start) < 15000) {
    if (mqtt.connect(MQTT_CLIENT_ID)) {
      mqtt.publish(TOPIC_LWT, "online", true);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(backoff));
    backoff = (backoff * 2 > maxBackoff) ? maxBackoff : backoff * 2;
  }
  return mqtt.connected();
}

bool mqttReconnect() {
  if (mqtt.connected()) return true;
  if (!connectMQTTWithBackoff()) {
    Serial.println("MQTT reconnect failed");
    return false;
  }
  Serial.println("MQTT connected");
  return true;
}

// Blynk handlers
BLYNK_WRITE(BLYNK_PIN_ADMIN) {
  int v = param.asInt();
  adminMode = (v != 0);
  Serial.printf("Blynk: adminMode = %d\n", (int)adminMode);
  if (adminMode && mqtt.connected()) mqtt.publish(TOPIC_HEARTBEAT, "admin_mode_on");
}

BLYNK_WRITE(BLYNK_V_LOCKCTRL) {
  int v = param.asInt();
  if (v == 1) {
    publishLockCommand(true, "MANUAL");
    if (mqtt.connected()) mqtt.publish(TOPIC_HEARTBEAT, "manual_open");
  }
  Blynk.virtualWrite(BLYNK_V_LOCKCTRL, 0);
}

BLYNK_WRITE(BLYNK_V_DELETEUID) {
  String s = param.asString();
  s.trim();
  s.toUpperCase();
  if (s.length() == 0) {
    Blynk.virtualWrite(BLYNK_V_AUTHSTAT, "DELETE FAILED: empty");
    return;
  }
  char buf[UID_MAX_LEN];
  s.toCharArray(buf, UID_MAX_LEN);
  bool ok = removeAuthorizedUid(buf);
  if (ok) {
    Blynk.virtualWrite(BLYNK_V_AUTHSTAT, String("DELETED: ") + s);
    Blynk.virtualWrite(BLYNK_V_TOTALUID, (int)authCount);
  } else {
    Blynk.virtualWrite(BLYNK_V_AUTHSTAT, String("DELETE FAILED: ") + s);
  }
}

BLYNK_WRITE(BLYNK_V_REQLIST) {
  int v = param.asInt();
  if (v == 1) {
    String list = buildUidListString();
    Blynk.virtualWrite(BLYNK_V_UIDLIST, list);
    Blynk.virtualWrite(BLYNK_V_REQLIST, 0);
  }
}

BLYNK_WRITE(BLYNK_V_AUTO_OFF) {
  int v = param.asInt();
  adminAutoOff = (v != 0);
  Serial.printf("Admin auto-off = %d\n", (int)adminAutoOff);
}

// Tasks
void taskRFID(void* pv) {
  (void)pv;
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID task started");
  uid_item_t item;
  for (;;) {
    if (!rfid.PICC_IsNewCardPresent()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    if (!rfid.PICC_ReadCardSerial()) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    uidToHexBuf(rfid.uid, item.uid, sizeof(item.uid));
    if (xQueueSend(qUIDs, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.println("Queue full: UID dropped");
    } else {
      Serial.printf("Scanned UID: %s\n", item.uid);
    }
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

void taskAuthAndMQTT(void* pv) {
  (void)pv;
  uid_item_t item;
  unsigned long lastBeat = 0;
  unsigned long beatInterval = 5000;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi not connected, attempting reconnect");
      if (!connectWiFiWithTimeout(10000)) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
    }
    if (!mqtt.connected()) mqttReconnect();
    else mqtt.loop();

    Blynk.run();

    if (millis() - lastBeat > beatInterval) {
      lastBeat = millis();
      if (mqtt.connected()) mqtt.publish(TOPIC_HEARTBEAT, MQTT_CLIENT_ID);
      Blynk.virtualWrite(BLYNK_V_HEARTBEAT, MQTT_CLIENT_ID);
      String sys = String("WiFi:") + (WiFi.status() == WL_CONNECTED ? "OK" : "DOWN") + " MQTT:" + (mqtt.connected() ? "OK" : "DOWN");
      Blynk.virtualWrite(BLYNK_V_SYSSTAT, sys);
      Blynk.virtualWrite(BLYNK_V_TOTALUID, (int)authCount);
    }

    if (xQueueReceive(qUIDs, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
      Blynk.virtualWrite(BLYNK_V_SCANUID, item.uid);

      if (adminMode) {
        bool added = addAuthorizedUid(item.uid);
        if (added) {
          Serial.printf("Admin: added UID %s\n", item.uid);
          if (mqtt.connected()) {
            char payload[128];
            snprintf(payload, sizeof(payload), "{\"event\":\"admin_add\",\"uid\":\"%s\",\"who\":\"%s\"}", item.uid, MQTT_CLIENT_ID);
            mqtt.publish(TOPIC_AUTH_RESULT, payload);
          }
          Blynk.virtualWrite(BLYNK_V_AUTHSTAT, String("ADDED: ") + item.uid);
          Blynk.virtualWrite(BLYNK_V_TOTALUID, (int)authCount);
        } else {
          Serial.printf("Admin: cannot add (exists or full) %s\n", item.uid);
          Blynk.virtualWrite(BLYNK_V_AUTHSTAT, String("ADD FAILED: ") + item.uid);
        }
        if (adminAutoOff) { adminMode = false; Blynk.virtualWrite(BLYNK_PIN_ADMIN, 0); }
        continue;
      }

      bool ok = isAuthorizedBuf(item.uid);
      if (ok) {
        publishLockCommand(true, item.uid);
        Blynk.virtualWrite(BLYNK_V_AUTHSTAT, String("AUTHORIZED: ") + item.uid);
      } else {
        publishLockCommand(false, item.uid);
        Serial.printf("Auth DENY for %s\n", item.uid);
        Blynk.virtualWrite(BLYNK_V_AUTHSTAT, String("DENIED: ") + item.uid);
        // no buzzer here - Lock board will handle deny buzzer
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("DualGuard Auth starting...");

  uint64_t mac = ESP.getEfuseMac();
  uint32_t mac_hi = (uint32_t)(mac >> 32);
  uint32_t mac_lo = (uint32_t)(mac & 0xFFFFFFFF);
  snprintf(MQTT_CLIENT_ID, sizeof(MQTT_CLIENT_ID), "DualGuardAuth%08X%08X", mac_hi, mac_lo);

  prefs.begin("dualguard", false);
  loadAuthListFromPrefs();

  qUIDs = xQueueCreate(8, sizeof(uid_item_t));
  if (qUIDs == nullptr) { Serial.println("Queue create failed"); while (true) vTaskDelay(pdMS_TO_TICKS(1000)); }

  if (!connectWiFiWithTimeout(20000)) Serial.println("WiFi connect failed");
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqttReconnect();

  Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASSWORD);

  Blynk.virtualWrite(BLYNK_V_TOTALUID, (int)authCount);
  Blynk.virtualWrite(BLYNK_V_UIDLIST, buildUidListString());
  Blynk.virtualWrite(BLYNK_V_AUTO_OFF, adminAutoOff ? 1 : 0);

  xTaskCreatePinnedToCore(taskRFID, "RFID", 4096, nullptr, 2, &taskRFIDHandle, 0);
  xTaskCreatePinnedToCore(taskAuthAndMQTT, "AuthMQTT", 8192, nullptr, 2, &taskAuthHandle, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
