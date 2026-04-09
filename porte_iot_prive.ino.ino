/*
 * PORTE CONNECTÉE - Contrôle depuis PARTOUT dans le monde
 * Arduino UNO R4 WiFi + Servo Moteur 360° + Capteurs de fin de course
 *
 * Fonctionnalités :
 * - Contrôle admin (ON/OFF)
 * - Codes jetables : un ami entre le code, porte s'ouvre UNE SEULE FOIS
 * - Ouverture mécanique par Servomoteur 360 avec capteurs (D2 et D4)
 * - Fermeture automatique après 3 secondes
 * - Indicateurs LED : Verte (D6) pendant ouverture/pause, Rouge (D7) pendant la fermeture
 * - Alarme sonore (D8) : activée/désactivée depuis l'interface admin (maintien du bouton)
 *
 * Bibliothèques nécessaires :
 * - ArduinoMqttClient (par Arduino)
 * - Servo (Standard)
 *
 * Branchements :
 *   D2  → Capteur fin de course OUVERTURE
 *   D4  → Capteur fin de course FERMETURE
 *   D6  → LED Verte (ouverture / porte ouverte)
 *   D7  → LED Rouge (fermeture en cours)
 *   D8  → Buzzer alarme
 *   D9  → Signal Servo 360°
 *   D13 → LED intégrée (état général)
 */

#include <WiFiS3.h>
#include <ArduinoMqttClient.h>
#include <Servo.h>

// === CONFIGURATION SERVO, BOUTONS, LEDS & BUZZER ===
Servo monServo;
const int SERVO_PIN    = 9;
const int ARRET_SERVO  = 90; // Valeur pour arrêter le servo 360 (ajuster si dérive)

const int PIN_BUTE_OUVERT = 2;  // Bouton fin d'ouverture (D2)
const int PIN_BUTE_FERME  = 4;  // Bouton fin de fermeture (D4)

const int PIN_LED_VERTE = 6;   // LED verte : ouverture + porte ouverte
const int PIN_LED_ROUGE = 7;   // LED rouge : fermeture en cours
const int PIN_BUZZER    = 8;   // Buzzer alarme
const int LED_PIN       = 13;  // LED intégrée Arduino

// Sécurité moteur : temps max pour atteindre un bouton (évite que le moteur crame)
const unsigned long TIMEOUT_MOTEUR = 5000;

// ===== CONFIGURATION WIFI =====
char ssid[] = "Wifi_acces";
char pass[] = "123456789abc";
// ================================

// ===== CONFIGURATION MQTT =====
const char broker[]           = "broker.hivemq.com";
int        port               = 1883;
const char topicCmd[]         = "porte-epcrd-2026/commande";
const char topicState[]       = "porte-epcrd-2026/etat";
const char topicAddCode[]     = "porte-epcrd-2026/addcode";    // admin ajoute un code
const char topicUseCode[]     = "porte-epcrd-2026/usecode";    // ami utilise un code
const char topicCodeResult[]  = "porte-epcrd-2026/coderesult"; // résultat (OK/FAIL)
const char topicCodeUsed[]    = "porte-epcrd-2026/codeused";   // notifie quel code a été utilisé
const char topicAlarme[]      = "porte-epcrd-2026/alarme";     // ON = sonne, OFF = arrête

bool ledState = false;

// ===== CODES JETABLES =====
const int MAX_CODES = 20;
String validCodes[MAX_CODES];
int codeCount = 0;

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

unsigned long lastAlive    = 0;
unsigned long autoCloseTime = 0; // Horodatage fermeture automatique

// ============================================================
//   FONCTIONS DE MOUVEMENT SERVO AVEC CAPTEURS
// ============================================================

void actionOuvrirMecanisme() {
  if (!ledState) { // On ouvre seulement si la porte est fermée
    digitalWrite(PIN_LED_VERTE, HIGH);
    digitalWrite(PIN_LED_ROUGE, LOW);

    Serial.println("-> Actionnement moteur : OUVERTURE");
    monServo.write(180); // Rotation sens ouverture

    unsigned long debut = millis();
    while (digitalRead(PIN_BUTE_OUVERT) == HIGH && (millis() - debut < TIMEOUT_MOTEUR)) {
      delay(10);
    }
    monServo.write(ARRET_SERVO); // Stop immédiat

    if (digitalRead(PIN_BUTE_OUVERT) == HIGH) {
      Serial.println("!! ALERTE : Bouton D2 non atteint (Timeout securite)");
    } else {
      Serial.println("-> Bouton D2 heurte ! Porte ouverte.");
    }
  }
}

void actionFermerMecanisme() {
  if (ledState) { // On ferme seulement si la porte est ouverte
    digitalWrite(PIN_LED_VERTE, LOW);
    digitalWrite(PIN_LED_ROUGE, HIGH);

    Serial.println("-> Actionnement moteur : FERMETURE");
    monServo.write(0); // Rotation sens fermeture

    unsigned long debut = millis();
    while (digitalRead(PIN_BUTE_FERME) == HIGH && (millis() - debut < TIMEOUT_MOTEUR)) {
      delay(10);
    }
    monServo.write(ARRET_SERVO); // Stop immédiat
    digitalWrite(PIN_LED_ROUGE, LOW);

    if (digitalRead(PIN_BUTE_FERME) == HIGH) {
      Serial.println("!! ALERTE : Bouton D4 non atteint (Timeout securite)");
    } else {
      Serial.println("-> Bouton D4 heurte ! Porte fermee.");
    }
  }
}

// ============================================================
//   CONNEXION WIFI & MQTT
// ============================================================

void connectWiFi() {
  Serial.print("Connexion WiFi a ");
  Serial.print(ssid);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi connecte !");
  Serial.print("IP locale : ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  Serial.print("Connexion au broker MQTT...");
  String clientId = "arduino-porte-" + String(random(10000));
  mqttClient.setId(clientId);

  while (!mqttClient.connect(broker, port)) {
    Serial.print(".");
    delay(2000);
  }
  Serial.println(" connecte !");

  mqttClient.subscribe(topicCmd);
  mqttClient.subscribe(topicAddCode);
  mqttClient.subscribe(topicUseCode);
  mqttClient.subscribe(topicAlarme);

  Serial.println("Ecoute sur : commande, addcode, usecode, alarme");

  mqttClient.beginMessage(topicState);
  mqttClient.print(ledState ? "ON" : "OFF");
  mqttClient.endMessage();

  Serial.println("\n====================================");
  Serial.println("   PRET ! Codes jetables + Alarme");
  Serial.println("====================================\n");
}

// ============================================================
//   GESTION DES CODES JETABLES
// ============================================================

void addCode(String code) {
  if (codeCount >= MAX_CODES) {
    Serial.println("!! Max codes atteint, suppression du plus ancien");
    for (int i = 0; i < MAX_CODES - 1; i++) validCodes[i] = validCodes[i + 1];
    codeCount = MAX_CODES - 1;
  }
  validCodes[codeCount++] = code;
  Serial.print("Code ajoute : "); Serial.print(code);
  Serial.print(" (total: "); Serial.print(codeCount); Serial.println(")");
}

bool useCode(String code) {
  for (int i = 0; i < codeCount; i++) {
    if (validCodes[i] == code) {
      Serial.print("Code VALIDE utilise : "); Serial.println(code);
      for (int j = i; j < codeCount - 1; j++) validCodes[j] = validCodes[j + 1];
      codeCount--;
      return true;
    }
  }
  Serial.print("Code INVALIDE : "); Serial.println(code);
  return false;
}

void openDoorTemporary() {
  actionOuvrirMecanisme();
  ledState = true;
  digitalWrite(LED_PIN, HIGH);
  Serial.println(">> PORTE OUVERTE (code jetable) - fermeture auto dans 3s");

  mqttClient.beginMessage(topicState);
  mqttClient.print("ON");
  mqttClient.endMessage();

  autoCloseTime = millis() + 3000;
}

// ============================================================
//   RÉCEPTION DES MESSAGES MQTT
// ============================================================

void onMqttMessage(int messageSize) {
  String topic = mqttClient.messageTopic();
  String message = "";
  while (mqttClient.available()) message += (char)mqttClient.read();

  Serial.print("["); Serial.print(topic); Serial.print("] "); Serial.println(message);

  // --- Commande admin ON/OFF ---
  if (topic == String(topicCmd)) {
    if (message == "ON") {
      actionOuvrirMecanisme();
      ledState = true;
      autoCloseTime = millis() + 3000;
      digitalWrite(LED_PIN, HIGH);
      Serial.println(">> OUVERTURE admin - fermeture auto 3s");
    } else if (message == "OFF") {
      actionFermerMecanisme();
      ledState = false;
      autoCloseTime = 0;
      digitalWrite(LED_PIN, LOW);
      Serial.println(">> FERMETURE admin");
    }
    mqttClient.beginMessage(topicState);
    mqttClient.print(ledState ? "ON" : "OFF");
    mqttClient.endMessage();
  }

  // --- Admin ajoute un code ---
  else if (topic == String(topicAddCode)) {
    addCode(message);
  }

  // --- Ami utilise un code ---
  else if (topic == String(topicUseCode)) {
    if (useCode(message)) {
      mqttClient.beginMessage(topicCodeResult);
      mqttClient.print("OK");
      mqttClient.endMessage();

      mqttClient.beginMessage(topicCodeUsed);
      mqttClient.print(message);
      mqttClient.endMessage();

      openDoorTemporary();
    } else {
      mqttClient.beginMessage(topicCodeResult);
      mqttClient.print("FAIL");
      mqttClient.endMessage();
    }
  }

  // --- Alarme sonore (admin maintient le bouton) ---
  else if (topic == String(topicAlarme)) {
    if (message == "ON") {
      tone(PIN_BUZZER, 1000); // Bip continu à 1kHz
      Serial.println(">> ALARME ACTIVEE (1kHz)");
    } else if (message == "OFF") {
      noTone(PIN_BUZZER);
      Serial.println(">> ALARME DESACTIVEE");
    }
  }
}

// ============================================================
//   SETUP & LOOP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Porte Connectee - Servo + LED + Alarme ===");

  // Capteurs fin de course avec résistance pull-up interne
  pinMode(PIN_BUTE_OUVERT, INPUT_PULLUP);
  pinMode(PIN_BUTE_FERME,  INPUT_PULLUP);

  // LEDs et buzzer
  pinMode(PIN_LED_VERTE, OUTPUT); digitalWrite(PIN_LED_VERTE, LOW);
  pinMode(PIN_LED_ROUGE, OUTPUT); digitalWrite(PIN_LED_ROUGE, LOW);
  pinMode(PIN_BUZZER,    OUTPUT); noTone(PIN_BUZZER);
  pinMode(LED_PIN,       OUTPUT); digitalWrite(LED_PIN, LOW);

  // Servo : s'assurer qu'il ne bouge pas au démarrage
  monServo.attach(SERVO_PIN);
  monServo.write(ARRET_SERVO);

  connectWiFi();
  connectMQTT();
  mqttClient.onMessage(onMqttMessage);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqttClient.connected()) connectMQTT();

  mqttClient.poll();

  // Fermeture automatique après 3 secondes
  if (autoCloseTime > 0 && millis() >= autoCloseTime) {
    autoCloseTime = 0;
    actionFermerMecanisme();
    ledState = false;
    digitalWrite(LED_PIN, LOW);
    Serial.println(">> FERMETURE AUTO (3s ecoulees) - Porte FERMEE");
    mqttClient.beginMessage(topicState);
    mqttClient.print("OFF");
    mqttClient.endMessage();
  }

  // Signe de vie toutes les 30 secondes (keep-alive MQTT)
  if (millis() - lastAlive > 30000) {
    lastAlive = millis();
    mqttClient.beginMessage(topicState);
    mqttClient.print(ledState ? "ON" : "OFF");
    mqttClient.endMessage();
  }
}