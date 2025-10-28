#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "config.h"

// Structures de données pour la gestion des nœuds
struct Node {
    String id;
    enum NodeType { UNKNOWN, AQUA_RESERV_PRO, WELLGUARD_PRO } type;
    long lastSeen;
    int rssi;
    String status; // ex: "FULL", "EMPTY", "ON", "OFF"
};

Node nodeList[MAX_NODES];
int nodeCount = 0;

// Objets globaux
AsyncWebServer server(80);
AsyncEventSource events("/events"); // Pour l'envoi de données en temps réel au tableau de bord

void handleLoRaPacket(String packet, int rssi);
void registerOrUpdateNode(String id, String type, String status, int rssi);
String getSystemStatusJson();

void setup() {
    Serial.begin(115200);

    // TODO: Implémenter la configuration WiFi (AP puis STA)
    WiFi.mode(WIFI_STA);
    WiFi.begin("VOTRE_SSID_WIFI", "VOTRE_MOT_DE_PASSE_WIFI");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected. IP: " + WiFi.localIP().toString());

    // Initialisation LoRa
    SPI.begin();
    LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
    if (!LoRa.begin(LORA_FREQ)) {
        Serial.println("Starting LoRa failed!");
        while (1);
    }
    LoRa.onReceive(onReceive);
    LoRa.receive(); // Mettre en mode réception
    Serial.println("LoRa Initialized and in receive mode.");

    // --- Configuration du Serveur Web ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", R"rawliteral(
            <!DOCTYPE HTML><html><head>
            <title>HydroControl-GE Dashboard</title>
            <meta name="viewport" content="width=device-width, initial-scale=1">
            </head><body>
            <h1>Tableau de Bord HydroControl-GE</h1>
            <div id="nodes">Chargement des noeuds...</div>
            <script>
            if (!!window.EventSource) {
             var source = new EventSource('/events');
             source.onmessage = function(e) {
                var data = JSON.parse(e.data);
                var html = "<h2>Noeuds Actifs (" + data.nodeCount + ")</h2><table border='1'><tr><th>ID</th><th>Type</th><th>RSSI</th><th>Status</th><th>Derni&egrave;re Vue</th></tr>";
                data.nodes.forEach(node => {
                    html += "<tr><td>" + node.id + "</td><td>" + node.type + "</td><td>" + node.rssi + "</td><td>" + node.status + "</td><td>" + new Date(node.lastSeen * 1000).toLocaleTimeString() + "</td></tr>";
                });
                html += "</table>";
                document.getElementById('nodes').innerHTML = html;
             }
            }
            </script></body></html>
        )rawliteral");
    });
    
    // API pour obtenir le statut
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", getSystemStatusJson());
    });

    // TODO: Ajouter les routes pour la configuration et l'assignation
    
    // Démarrer le serveur
    server.begin();
}

void loop() {
    // Le travail est fait par les callbacks et les tâches
    // On peut envoyer périodiquement l'état aux clients web connectés
    events.send(getSystemStatusJson().c_str(), "update", millis());
    delay(2000);
}

// Callback LoRa, s'exécute sur interruption
void onReceive(int packetSize) {
    if (packetSize == 0) return;

    String packet = "";
    while (LoRa.available()) {
        packet += (char)LoRa.read();
    }
    int rssi = LoRa.packetRssi();

    // NOTE: On ne peut pas faire de traitement long ici (contexte d'ISR)
    // Idéalement, on mettrait ce paquet dans une queue FreeRTOS pour traitement
    // Pour simplifier, on traite directement, mais attention aux blocages.
    handleLoRaPacket(packet, rssi);
}

void handleLoRaPacket(String packet, int rssi) {
    Serial.printf("Centrale received LoRa packet: %s with RSSI %d\n", packet.c_str(), rssi);
    
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, packet);

    if (error) return;

    // Authentification
    const char* key = doc["key"];
    if (strcmp(key, PRE_SHARED_KEY) != 0) {
        Serial.println("Invalid Pre-Shared Key received by Centrale!");
        return;
    }

    const char* type = doc["type"];
    const char* sourceId = doc["sourceId"];
    
    // TODO: Détecter les paquets de "discovery"
    // TODO: Traiter les paquets de "status" des noeuds
    // Pour cette démo, on traite un paquet "ACK_PUMP" comme un rapport de statut
    if (strcmp(type, "ACK_PUMP") == 0) {
        const char* status = doc["status"];
        registerOrUpdateNode(sourceId, "WellguardPro", status, rssi);
    }
    // TODO: Ajouter le traitement pour les paquets de statut d'AquaReservPro
}

void registerOrUpdateNode(String id, String typeStr, String status, int rssi) {
    int existingNodeIndex = -1;
    for (int i = 0; i < nodeCount; i++) {
        if (nodeList[i].id.equals(id)) {
            existingNodeIndex = i;
            break;
        }
    }

    if (existingNodeIndex != -1) { // Mise à jour
        nodeList[existingNodeIndex].lastSeen = millis();
        nodeList[existingNodeIndex].rssi = rssi;
        nodeList[existingNodeIndex].status = status;
    } else if (nodeCount < MAX_NODES) { // Nouveau noeud
        nodeList[nodeCount].id = id;
        nodeList[nodeCount].type = (typeStr == "WellguardPro") ? Node::WELLGUARD_PRO : Node::AQUA_RESERV_PRO;
        nodeList[nodeCount].lastSeen = millis();
        nodeList[nodeCount].rssi = rssi;
        nodeList[nodeCount].status = status;
        nodeCount++;
    }
}

String getSystemStatusJson() {
    StaticJsonDocument<1024> doc;
    doc["nodeCount"] = nodeCount;
    JsonArray nodes = doc.createNestedArray("nodes");

    for (int i = 0; i < nodeCount; i++) {
        JsonObject node = nodes.createNestedObject();
        node["id"] = nodeList[i].id;
        node["type"] = (nodeList[i].type == Node::WELLGUARD_PRO) ? "WellguardPro" : "AquaReservPro";
        node["rssi"] = nodeList[i].rssi;
        node["status"] = nodeList[i].status;
        node["lastSeen"] = nodeList[i].lastSeen;
    }

    String output;
    serializeJson(doc, output);
    return output;
}
