# Porte IoT Sécurisée (Porte Connectée)

Ce projet permet de contrôler l'ouverture et la fermeture d'une porte à distance (partout dans le monde) via un système IoT composé d'un microcontrôleur Arduino UNO R4 WiFi, d'un serveur Node.js, et d'une interface web.

## Fonctionnalités

- **Contrôle Admin** : Interface web pour ouvrir ou fermer la porte (ON/OFF).
- **Codes Temporaires (AirBnB)** : Génération de codes à usage unique ou temporaires pour autoriser l'accès.
- **Ouverture Mécanique** : Utilisation d'un servomoteur 360° avec des capteurs de fin de course pour détecter si la porte est ouverte ou fermée.
- **Fermeture Automatique** : La porte se referme automatiquement après quelques secondes.
- **Indicateurs et Alertes** : 
  - LEDs (Verte pour l'ouverture, Rouge pour la fermeture).
  - Alarme sonore (Buzzer) activable par l'admin.
- **Communication Temps Réel** : Protocole MQTT pour la communication entre le serveur et l'Arduino, avec des WebSockets pour les interfaces utilisateurs.

## Structure du Projet

- `porte_iot_prive.ino.ino` : Code source Arduino (C++) pour le microcontrôleur UNO R4 WiFi. Gère le WiFi, MQTT, le servomoteur, les capteurs et l'alarme.
- `server/` : Dossier contenant le serveur Node.js backend.
  - `server.js` : Serveur Express + Socket.io + Client MQTT. Gère les connexions WebSocket des clients et la logique d'accès.
  - `database.json` : Stockage local des codes générés et de l'historique d'accès.
- `porte_AirBnB.html` : Interface web utilisateur pour entrer un code d'accès temporaire.
- `Porte_geek.html` : Interface d'administration pour la gestion complète de la porte.

## Prérequis

- Arduino IDE (avec bibliothèque ArduinoMqttClient et Servo).
- Node.js & npm (pour exécuter le serveur local ou distant).
- Broker MQTT (ex: broker.hivemq.com par défaut).

## Déploiement

1. Flashez le code Arduino sur votre UNO R4 WiFi en modifiant vos identifiants réseau.
2. Démarrez le serveur Node.js (`cd server && npm install && node server.js`).
3. Accédez aux interfaces web pour interagir avec le système.