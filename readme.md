# Kufuli SmartLock – Système de Serrure Intelligente basé sur ESP32 et FreeRTOS



**Mémoire :** Adaptation de FreeRTOS pour un fonctionnement optimal de Kufuli SmartLock

## Description
Ce projet migre et optimise la serrure intelligente Kufuli (initialement sur microcontrôleur TLSR8251) vers l'ESP32 avec FreeRTOS.  
Fonctionnalités ajoutées :
- Déverrouillage par télécommande IR
- Détection vandalisme (tilt switch + alarme buzzer)
- Surveillance température/humidité (DHT11)
- Gestion blocage après 3 tentatives échouées
- Contrôle distant via WiFi + interface web simple
- Verrouillage automatique après 5 secondes

## Matériel requis
- ESP32 DevKit (ou équivalent)
- Servo moteur (ex. SG90)
- Module RFID MFRC522
- Récepteur IR + télécommande
- Capteur DHT11
- Capteur tilt switch
- Buzzer actif
- Clavier matriciel 4x4
- Module empreinte digitale (compatible Adafruit AS608)
- Breadboard, résistances, câbles

## Bibliothèques Arduino nécessaires
- WiFi (native ESP32)
- WebServer (native ESP32)
- BluetoothSerial (native ESP32)
- ESP32Servo
- MFRC522
- IRremote
- Adafruit Fingerprint Sensor Library
- DHT sensor library
- Keypad

Installez-les via le Gestionnaire de bibliothèques de l'IDE Arduino.

## Installation et utilisation
1. Ouvrir `SmartLock.ino` dans l'IDE Arduino
2. Sélectionner la carte **ESP32 Dev Module**
3. Modifier les identifiants WiFi si besoin (ssid et password)
4. Compiler → Téléverser
5. Ouvrir le moniteur série (115200 bauds) pour voir l'IP et tester

Accès WiFi : http://<IP>/unlock?password=237  
Bluetooth : envoyer "237" au périphérique "ESP32_SmartLock"

## Auteur
**Mimche Pempeme Fadimatou**  
Février 2026  
Projet académique – Mémoire de fin d’études

## Licence
Voir LICENSE (MIT par défaut)