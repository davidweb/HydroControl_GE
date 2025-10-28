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
TaskHandle_t Task_LoRa_Manager_Handle;
TaskHandle_t Task_GPIO_Handler_Handle;

// Variables d'état volatiles
volatile bool relayState = false;
volatile long lastCommandRssi = 0;
String deviceId;

// Prototypes de fonctions
void Task_LoRa_Manager(void *pvParameters);
void Task_GPIO_Handler(void *pvParameters);
void setupLoRa();
void sendStatusUpdate();
void handleLoRaPacket(String packet);
void setRelayState(bool newState);
void setupInitialConfigAP();

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW); // Assurer que la pompe est éteinte au démarrage
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Obtenir l'ID unique
    deviceId = WiFi.macAddress();
    deviceId.replace(":", "");
    Serial.println("WellguardPro ID: " + deviceId);

    // Initialisation LoRa
    setupLoRa();

    // Démarrage des tâches FreeRTOS
    xTaskCreate(Task_LoRa_Manager, "LoRa Manager", 4096, NULL, 1, &Task_LoRa_Manager_Handle);
    xTaskCreate(Task_GPIO_Handler, "GPIO Handler", 2048, NULL, 2, &Task_GPIO_Handler_Handle);

    // TODO: Implémenter la logique de configuration initiale via AP
    // Pour l'instant, on démarre directement
    // setupInitialConfigAP();
}

void loop() {
    vTaskDelay(portMAX_DELAY); // La loop d'Arduino n'est pas utilisée
}

void setupLoRa() {
    SPI.begin();
    LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
    if (!LoRa.begin(LORA_FREQ)) {
        Serial.println("Starting LoRa failed!");
        while (1);
    }
    Serial.println("LoRa Initialized.");
}

void setRelayState(bool newState) {
    relayState = newState;
    digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
    Serial.printf("Relay state set to: %s\n", relayState ? "ON" : "OFF");
    // TODO: Ajouter la persistance de l'état si nécessaire
}

// Tâche pour gérer la communication LoRa
void Task_LoRa_Manager(void *pvParameters) {
    for (;;) {
        int packetSize = LoRa.parsePacket();
        if (packetSize) {
            String received = "";
            while (LoRa.available()) {
                received += (char)LoRa.read();
            }
            lastCommandRssi = LoRa.packetRssi();
            Serial.print("Received LoRa packet: '");
            Serial.print(received);
            Serial.print("' with RSSI: ");
            Serial.println(lastCommandRssi);

            handleLoRaPacket(received);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// Tâche pour gérer le bouton physique
void Task_GPIO_Handler(void *pvParameters) {
    bool lastButtonState = HIGH;
    unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 50;

    for (;;) {
        bool currentButtonState = digitalRead(BUTTON_PIN);
        if (currentButtonState != lastButtonState) {
            lastDebounceTime = millis();
        }

        if ((millis() - lastDebounceTime) > debounceDelay) {
            if (currentButtonState == LOW) { // Bouton pressé
                // NOTE: La logique manuelle est principalement sur AquaReservPro.
                // Ici, le bouton pourrait forcer un état et le signaler.
                // Pour V2.0, on ignore le bouton sur WellguardPro comme non spécifié.
            }
        }
        lastButtonState = currentButtonState;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}


void handleLoRaPacket(String packet) {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, packet);

    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
    }

    const char* type = doc["type"];
    const char* key = doc["key"];
    const char* targetId = doc["targetId"];

    if (strcmp(key, PRE_SHARED_KEY) != 0) {
        Serial.println("Invalid Pre-Shared Key!");
        return;
    }

    if (deviceId.equals(targetId)) {
        if (strcmp(type, "CMD_PUMP") == 0) {
            bool command = doc["command"]; // "ON" ou "OFF"
            setRelayState(command);

            // Envoyer un acquittement
            StaticJsonDocument<200> ackDoc;
            ackDoc["type"] = "ACK_PUMP";
            ackDoc["sourceId"] = deviceId;
            ackDoc["status"] = relayState ? "ON" : "OFF";
            ackDoc["key"] = PRE_SHARED_KEY;

            String ackPacket;
            serializeJson(ackDoc, ackPacket);

            LoRa.beginPacket();
            LoRa.print(ackPacket);
            LoRa.endPacket();
            Serial.println("Sent ACK packet.");
        }
    }
}

// TODO: Implémenter la fonction de rapport d'état périodique vers la centrale
void sendStatusUpdate() {
    // Cette fonction sera appelée périodiquement par une tâche dédiée
}

// TODO: Implémenter la logique pour le point d'accès de configuration initiale
void setupInitialConfigAP() {
    // Créer un AP, servir une page simple pour configurer la clé partagée
}
