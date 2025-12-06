#include <WiFi.h>
#include <PubSubClient.h>

// ========= WiFi & MQTT ==========
const char* WIFI_SSID     = "Orang Cerdas";
const char* WIFI_PASSWORD = "12345678e";

const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;

const char* MQTT_CLIENT_ID = "DualGuard-Lock-ESP32";
const char* TOPIC_LOCK_CMD = "dualguard/lock/cmd";

// ========= GPIO ==========
static const int LOCK_PIN = 27;

// ========= RTOS ==========
QueueHandle_t qCmds = nullptr;
TaskHandle_t taskMQTTHandle = nullptr;
TaskHandle_t taskLockHandle = nullptr;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// =====================================
// WIFI + MQTT CONNECTION
// =====================================
bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(200);
  }

  return WiFi.status() == WL_CONNECTED;
}

bool connectMQTT() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);

  unsigned long start = millis();  
  while (!mqtt.connected() && millis() - start < 10000) {
    mqtt.connect(MQTT_CLIENT_ID);
    delay(200);
  }

  if (mqtt.connected()) {
    mqtt.subscribe(TOPIC_LOCK_CMD);
  }

  return mqtt.connected();
}

// =====================================
// HARDWARE ACTION
// =====================================
void unlockPulse(uint32_t ms = 10000) {
  digitalWrite(LOCK_PIN, HIGH);
  delay(ms);
  digitalWrite(LOCK_PIN, LOW);
}

// =====================================
// MQTT CALLBACK
// =====================================
void onMessage(char* topic, byte* payload, unsigned int length) {
  String msg;

  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  char cmd[8] = {0};

  if (msg.indexOf("\"cmd\":\"open\"") >= 0) {
    strcpy(cmd, "open");
  } 
  else if (msg.indexOf("\"cmd\":\"deny\"") >= 0) {
    strcpy(cmd, "deny");
  } 
  else {
    return;
  }

  xQueueSend(qCmds, cmd, 0);
}

// =====================================
// MQTT TASK
// =====================================
void taskMQTT(void* pv) {
  (void)pv;

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!mqtt.connected()) {
        connectMQTT();
      } else {
        mqtt.loop();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =====================================
// LOCK TASK
// =====================================
void taskLock(void* pv) {
  (void)pv;

  char cmdBuf[8] = {0};

  for (;;) {
    if (xQueueReceive(qCmds, cmdBuf, pdMS_TO_TICKS(100)) == pdTRUE) {

      if (strcmp(cmdBuf, "open") == 0) {
        Serial.println("Lock: OPEN -> GPIO27 HIGH for 10s");
        unlockPulse(10000);

      } else if (strcmp(cmdBuf, "deny") == 0) {
        Serial.println("Lock: DENY -> no action");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =====================================
// SETUP
// =====================================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(LOCK_PIN, OUTPUT);
  digitalWrite(LOCK_PIN, LOW);

  connectWiFi();
  mqtt.setCallback(onMessage);
  connectMQTT();

  qCmds = xQueueCreate(8, sizeof(char[8]));
  if (!qCmds) {
    Serial.println("Queue creation FAILED");
  }

  xTaskCreatePinnedToCore(taskMQTT, "MQTT", 4096, nullptr, 2, &taskMQTTHandle, 0);
  xTaskCreatePinnedToCore(taskLock,  "LOCK", 4096, nullptr, 2, &taskLockHandle, 1);
}

// =====================================
// LOOP
// =====================================
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
