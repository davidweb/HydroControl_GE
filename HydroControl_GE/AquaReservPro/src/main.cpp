#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "config.h"

// Objets globaux
Preferences preferences;
AsyncWebServer server(80);

// Variables d'état globales (protégées par la logique des tâches)
String deviceId;
String assignedWellId = "";
OperatingMode currentMode = AUTO;
LevelState currentLevel = LEVEL_UNKNOWN;
bool currentPumpCommand = false; // La commande que nous voulons pour la pompe

// Prototypes
void Task_Control_Logic(void *pvParameters);
void Task_Sensor_Handler(void *pvParameters);
void Task_LoRa_Manager(void *pvParameters);
void sendPumpCommand(bool command);
void loadConfiguration();
void saveConfiguration();

void setup() {
    Serial.begin(115200);
    pinMode(LEVEL_SENSOR_PIN, INPUT_PULLUP);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    deviceId = WiFi.macAddress();
    deviceId.replace(":", "");
    Serial.println("AquaReservPro ID: " + deviceId);

    // Charger la configuration depuis la mémoire non-volatile
    loadConfiguration();

    // Initialisation LoRa
    SPI.begin();
    LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
    if (!LoRa.begin(LORA_FREQ)) {
        Serial.println("Starting LoRa failed!");
        while (1);
    }
    Serial.println("LoRa Initialized.");

    // Démarrage des tâches FreeRTOS
    xTaskCreate(Task_Sensor_Handler, "Sensor Handler", 2048, NULL, 2, NULL);
    xTaskCreate(Task_Control_Logic, "Control Logic", 4096, NULL, 1, NULL);
    xTaskCreate(Task_LoRa_Manager, "LoRa Manager", 4096, NULL, 1, NULL);

    // TODO: Démarrer le serveur web pour la configuration locale
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}

void loadConfiguration() {
    preferences.begin("hydro_config", false);
    currentMode = (OperatingMode)preferences.getUChar(PREF_KEY_MODE, AUTO);
    assignedWellId = preferences.getString(PREF_KEY_WELL_ID, "");
    preferences.end();

    Serial.println("Configuration loaded:");
    Serial.printf(" - Mode: %s\n", currentMode == AUTO ? "AUTO" : "MANUAL");
    Serial.printf(" - Assigned Well ID: %s\n", assignedWellId.c_str());
}

void saveConfiguration() {
    preferences.begin("hydro_config", false);
    preferences.putUChar(PREF_KEY_MODE, (unsigned char)currentMode);
    preferences.putString(PREF_KEY_WELL_ID, assignedWellId);
    preferences.end();
    Serial.println("Configuration saved.");
}

// Tâche de lecture et filtrage du capteur de niveau
void Task_Sensor_Handler(void *pvParameters) {
    LevelState lastUnstableLevel = LEVEL_UNKNOWN;
    unsigned long levelChangeTimestamp = 0;

    for (;;) {
        // Lecture : contact = plein (LOW), pas de contact = vide (HIGH)
        bool rawValue = digitalRead(LEVEL_SENSOR_PIN);
        LevelState detectedLevel = (rawValue == LOW) ? LEVEL_FULL : LEVEL_EMPTY;

        if (detectedLevel != lastUnstableLevel) {
            lastUnstableLevel = detectedLevel;
            levelChangeTimestamp = millis();
        }

        if (millis() - levelChangeTimestamp > SENSOR_STABILITY_MS) {
            if (currentLevel != detectedLevel) {
                currentLevel = detectedLevel;
                Serial.printf("New stable level detected: %s\n", currentLevel == LEVEL_FULL ? "FULL" : "EMPTY");
            }
        }
        vTaskDelay(250 / portTICK_PERIOD_MS); // Vérifier 4x par seconde
    }
}

// Tâche implémentant la logique de commande
void Task_Control_Logic(void *pvParameters) {
    bool lastSentPumpCommand = !currentPumpCommand;

    for (;;) {
        if (currentMode == AUTO) {
            if (currentLevel == LEVEL_EMPTY) {
                currentPumpCommand = true; // ON
            } else if (currentLevel == LEVEL_FULL) {
                currentPumpCommand = false; // OFF
            }
        }
        // Le mode MANUEL est géré par des interruptions/événements (bouton, web)
        // qui modifient directement currentPumpCommand

        // Si la commande a changé, on l'envoie
        if (currentPumpCommand != lastSentPumpCommand) {
            Serial.printf("Logic decided to change pump state to: %s\n", currentPumpCommand ? "ON" : "OFF");
            sendPumpCommand(currentPumpCommand);
            lastSentPumpCommand = currentPumpCommand;
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// Tâche de gestion des communications LoRa (émission et réception d'ACKs)
void Task_LoRa_Manager(void *pvParameters) {
    for (;;) {
        int packetSize = LoRa.parsePacket();
        if (packetSize) {
            String received = "";
            while (LoRa.available()) {
                received += (char)LoRa.read();
            }
            Serial.printf("LoRa Received ACK/STATUS: %s\n", received.c_str());
            // TODO: Traiter les ACKs et les messages de la centrale
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void sendPumpCommand(bool command) {
    if (assignedWellId.isEmpty()) {
        Serial.println("Cannot send command: No WellGuardPro assigned.");
        return;
    }

    StaticJsonDocument<256> doc;
    doc["type"] = "CMD_PUMP";
    doc["sourceId"] = deviceId;
    doc["targetId"] = assignedWellId;
    doc["command"] = command;
    doc["key"] = PRE_SHARED_KEY;

    String packet;
    serializeJson(doc, packet);

    // TODO: Implémenter une logique de réessai avec acquittement
    // Pour l'instant, envoi simple
    LoRa.beginPacket();
    LoRa.print(packet);
    LoRa.endPacket();

    Serial.printf("Sent LoRa command to %s: %s\n", assignedWellId.c_str(), packet.c_str());
}
