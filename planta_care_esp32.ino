#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "time.h"
#include "DHT.h"  
#include "config.h"
#include "Adafruit_VEML7700.h" 

// 🏷️ Sensor and Actuator Pins
#define DHT_PIN 13
#define DHT_TYPE DHT22
#define SOIL_SENSOR 34
#define LED_PIN 2
#define SDA_PIN  25 
#define SCL_PIN  26

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -10800;
const int daylightOffset_sec = 0;

DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_VEML7700 veml; 

unsigned long lastRealTimeSent = 0; 
unsigned long lastRealTimeSeen = 0;
unsigned long lastCheckTime = 0;
bool realTimeActive = false;
int lastSentMinute = -1;
bool lastRealTimeEnabledState = false;

const unsigned long HTTP_TIMEOUT = 10000;  
const unsigned long RETRY_DELAY = 2000;    
const int MAX_RETRIES = 3;
const int JSON_DOCUMENT_SIZE = 300;

// 📶 Connect to Wi-Fi
void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Conectando ao Wi-Fi...");
    
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 30) {
        delay(1000);
        Serial.print(".");
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✅ Wi-Fi conectado!");
    } else {
        Serial.println("\n❌ Falha ao conectar. Reiniciando...");
        delay(5000);
        ESP.restart();
    }
}

// ⏳ Configure time via NTP
void setupTime() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("⏳ Sincronizando horário...");
    
    struct tm timeinfo;
    int retry = 0;
    const int maxRetries = 10;
    
    while (!getLocalTime(&timeinfo) && retry < maxRetries) {
        Serial.println("⏳ Aguardando sincronização NTP...");
        delay(1000);
        retry++;
    }

    if (retry >= maxRetries) {
        Serial.println("❌ Falha ao sincronizar horário!");
        ESP.restart();
    } else {
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%d/%m/%Y %H:%M:%S", &timeinfo);
        Serial.print("✅ Horário sincronizado: ");
        Serial.println(strftime_buf);
    }
}

// 🕒 Get formatted time
String getFormattedTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return "Timestamp error";

    char timestamp[30];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return String(timestamp);
}

// 🔄 Check if real-time sending is enabled
bool isRealTimeEnabled() {
    HTTPClient http;

    String macAddress = WiFi.macAddress();
    macAddress.replace(":", "%3A");

    String url = "https://";
    url += FIREBASE_PROJECT_ID;
    url += "-default-rtdb.firebaseio.com/devices/";
    url += macAddress;
    url += "/realTimeEnabled.json?auth=";
    url += FIREBASE_API_KEY;

    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
        String response = http.getString();
        http.end();

        Serial.print("🔍 Resposta do Realtime: ");
        Serial.println(response);

        if (response == "true") {
            realTimeActive = true;
            return true;
        } else {
            realTimeActive = false;
            return false;
        }
    } else {
        Serial.print("❌ Erro HTTP: ");
        Serial.println(httpResponseCode);
        realTimeActive = false;  
    }

    http.end();
    return false;
}

// 🚨 Disable realTimeEnabled after 2 minutes
void disableRealTimeInFirestore() {
    HTTPClient http;

    String macAddress = WiFi.macAddress();
    macAddress.replace(":", "%3A");

    String url = "https://" + String(FIREBASE_PROJECT_ID) + "-default-rtdb.firebaseio.com/devices/";
    url += macAddress;
    url += "/realTimeEnabled.json?auth=" + String(FIREBASE_API_KEY);

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.PUT("false");

    if (httpResponseCode > 0) {
        Serial.println("✅ realTimeEnabled desativado no Realtime DB!");
    } else {
        Serial.print("❌ Erro ao desativar realTimeEnabled no Realtime DB: ");
        Serial.println(httpResponseCode);
    }

    http.end();
    realTimeActive = false;
}

float sanitizeReading(float value) {
    return isnan(value) ? 0.0 : value;
}

// 🔄 Send data to Firestore (with retry)
bool sendDataToFirestore(float moisture, float temperature, float humidity, float light, String timestamp, bool realTime) {
    moisture = sanitizeReading(moisture);
    temperature = sanitizeReading(temperature);
    humidity = sanitizeReading(humidity);

    int wifiRetries = 0;
    while (WiFi.status() != WL_CONNECTED && wifiRetries < 3) {
        Serial.println("🚨 Wi-Fi desconectado! Tentando reconectar...");
        connectWiFi();
        wifiRetries++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ Falha definitiva na conexão WiFi");
        return false;
    }

    String macAddress = WiFi.macAddress();
    macAddress.replace(":", "%3A");

    String url = "https://firestore.googleapis.com/v1/projects/" + 
                String(FIREBASE_PROJECT_ID) + 
                "/databases/(default)/documents/devices/" + 
                macAddress;

    url += realTime ? "?key=" : "/readings/" + timestamp + "?key=";
    url += String(FIREBASE_API_KEY);

    String moistureFormatted = String(moisture, 1); 
    double moistureDouble = moistureFormatted.toDouble();

    StaticJsonDocument<JSON_DOCUMENT_SIZE> doc;
    try {
        doc["fields"]["timestamp"]["stringValue"] = timestamp;  
        doc["fields"]["moisture"]["doubleValue"] = moistureDouble;
        doc["fields"]["temperature"]["doubleValue"] = temperature;
        doc["fields"]["humidity"]["doubleValue"] = humidity;    
        doc["fields"]["light"]["doubleValue"] = light;  
    } catch (...) {
        Serial.println("❌ Erro ao preparar JSON");
        return false;
    }

    String jsonData;
    serializeJson(doc, jsonData);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(HTTP_TIMEOUT);

    int retryCount = 0;
    while (retryCount < MAX_RETRIES) {
        int httpResponseCode = http.PATCH(jsonData);
        
        if (httpResponseCode > 0) {
            Serial.print("✅ Dados enviados com sucesso! Código: ");
            Serial.println(httpResponseCode);
            http.end();
            return true;
        } else {
            Serial.printf("❌ Falha ao enviar (tentativa %d/%d) - Código: %d\n", 
                         retryCount + 1, MAX_RETRIES, httpResponseCode);
            
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("🔄 Reconectando WiFi...");
                connectWiFi();
            }
            
            delay(RETRY_DELAY * (retryCount + 1));
        }
        retryCount++;
    }

    Serial.println("❌ Erro definitivo: não foi possível enviar os dados após todas as tentativas");
    http.end();
    return false;
}

void setup() {
    Serial.begin(115200);
    connectWiFi();
    setupTime();
    dht.begin();

    Wire.begin(SDA_PIN, SCL_PIN);
    if (!veml.begin()) {
        Serial.println("❌ Sensor VEML7700 não encontrado!");
        while (1);
    }
    Serial.println("✅ Sensor VEML7700 detectado!");

    pinMode(LED_PIN, OUTPUT);
}

void loop() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    int minutes = timeinfo.tm_min;
    int seconds = timeinfo.tm_sec;
    unsigned long now = millis();

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    int soilValue = analogRead(SOIL_SENSOR);
    float moisture = (100.0 * (5000 - soilValue)) / 3900.0;
    float light = veml.readLux(); 
    moisture = constrain(moisture, 0.0, 100.0);
    String timestamp = getFormattedTime();

    // 🔄 Check if real-time sending is activated every 1 second
    if (now - lastCheckTime > 1000) {
        bool enabled = isRealTimeEnabled();

        if (enabled && !lastRealTimeEnabledState) {
            Serial.println("🟢 RealTime ativado!");
            lastRealTimeSeen = now;
            realTimeActive = true;
            sendDataToFirestore(moisture, temperature, humidity, light, timestamp, true);
            lastRealTimeSent = now;
        } else if (!enabled && realTimeActive) {
            Serial.println("🔴 RealTime foi desativado remotamente!");
            realTimeActive = false;
        }

        lastRealTimeEnabledState = enabled;
        lastCheckTime = now;
    }

    // 🔄 Sends every 10 seconds while realTimeActive is active
    if (realTimeActive && (now - lastRealTimeSeen <= 120000)) {
        if (now - lastRealTimeSent >= 10000) { 
            sendDataToFirestore(moisture, temperature, humidity, light, timestamp, true);
            lastRealTimeSent = now;  
        }
    }

    // ⏳ Deactivates realTimeEnabled after 2 minutes
    if (realTimeActive && (now - lastRealTimeSeen > 120000)) {
        Serial.println(lastRealTimeSeen);
        Serial.println("⏱️ Tempo expirado. Desativando realTime...");
        disableRealTimeInFirestore();
        realTimeActive = false;
    }

    // ⏳ Scheduled send every 30 minutes
    if ((minutes % 30 == 0) && (seconds <= 10) && (minutes != lastSentMinute)) {
        Serial.println("🔄 Iniciando envio programado...");
        sendDataToFirestore(moisture, temperature, humidity, light, timestamp, false);
        lastSentMinute = minutes;
        Serial.println("✅ Dados enviados com sucesso!");
    }

    delay(500);
}