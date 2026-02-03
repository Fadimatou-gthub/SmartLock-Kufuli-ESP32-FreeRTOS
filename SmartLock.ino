#include <WiFi.h>
#include <WebServer.h>
#include <BluetoothSerial.h>
#include <ESP32Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <IRremote.h>
#include <Adafruit_Fingerprint.h>
#include <DHT.h>
#include <Keypad.h>

// Configuration WiFi
const char* ssid = "Fadimatou";
const char* password = "0987654321";
const String WIFI_BT_PASSWORD = "237";

// Pins
#define SERVO_PIN 16
#define RFID_SDA 5
#define RFID_RST 17
#define IR_RECV_PIN 4
#define DHT_PIN 2
#define TILT_PIN 34
#define BUZZER_PIN 13
#define FINGER_RX 21
#define FINGER_TX 22

// Configuration clavier 4x4
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {32, 33, 25, 26};
byte colPins[COLS] = {27, 14, 12, 15};

// Objets
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
Servo doorServo;
MFRC522 mfrc522(RFID_SDA, RFID_RST);
BluetoothSerial SerialBT;
WebServer server(80);
DHT dht(DHT_PIN, DHT11);
HardwareSerial fingerSerial(1);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

// Variables globales
const String KEYPAD_PASSWORD = "8421";
const uint32_t IR_VALID_CODE = 0xBD42FF00;
byte validUIDs[2][4] = {
  {0xCD, 0xCF, 0xFC, 0x03},
  {0xB3, 0x3F, 0x6A, 0x2D}
};

enum LockMethod { NONE, KEYPAD, RFID, FINGERPRINT, IR, WIFI, BLUETOOTH };
struct FailureTracker {
  int count;
  unsigned long blockUntil;
  LockMethod method;
};

FailureTracker failures[7];
bool doorOpen = false;
float temperature = 0;
float humidity = 0;
SemaphoreHandle_t servoMutex;
QueueHandle_t unlockQueue;

// Position servo
const int LOCKED_ANGLE = 0;
const int UNLOCKED_ANGLE = 90;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Demarrage Serrure Intelligente ===");
  
  // Initialisation pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TILT_PIN, INPUT_PULLUP);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Test buzzer au démarrage
  beepSuccess();
  
  // Servo
  doorServo.attach(SERVO_PIN);
  doorServo.write(LOCKED_ANGLE);
  Serial.println("Servo initialise");
  
  // SPI et RFID
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("RFID initialise");
  
  // IR
  IrReceiver.begin(IR_RECV_PIN, ENABLE_LED_FEEDBACK);
  Serial.println("IR initialise");
  
  // Capteur empreinte
  fingerSerial.begin(57600, SERIAL_8N1, FINGER_RX, FINGER_TX);
  finger.begin(57600);
  if (finger.verifyPassword()) {
    Serial.println("Capteur empreinte detecte");
  } else {
    Serial.println("Capteur empreinte non detecte");
  }
  
  // DHT11
  dht.begin();
  Serial.println("DHT11 initialise");
  
  // WiFi
  Serial.print("Connexion WiFi a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int wifiTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
    delay(500);
    Serial.print(".");
    wifiTimeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connecte!");
    Serial.print("Adresse IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("URL de deverrouillage: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/unlock?password=237");
  } else {
    Serial.println("\nEchec connexion WiFi");
  }
  
  // Serveur Web
  server.on("/", handleRoot);
  server.on("/unlock", handleWebUnlock);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Serveur Web demarre");
  
  // Bluetooth
  if (SerialBT.begin("ESP32_SmartLock")) {
    Serial.println("Bluetooth initialise: ESP32_SmartLock");
    Serial.println("Envoyez '237' via Bluetooth pour deverrouiller");
  } else {
    Serial.println("Echec initialisation Bluetooth");
  }
  
  // FreeRTOS
  servoMutex = xSemaphoreCreateMutex();
  unlockQueue = xQueueCreate(5, sizeof(LockMethod));
  
  xTaskCreate(taskKeypad, "Keypad", 4096, NULL, 2, NULL);
  xTaskCreate(taskRFID, "RFID", 4096, NULL, 2, NULL);
  xTaskCreate(taskIR, "IR", 4096, NULL, 2, NULL);
  xTaskCreate(taskFingerprint, "Fingerprint", 4096, NULL, 2, NULL);
  xTaskCreate(taskBluetooth, "Bluetooth", 4096, NULL, 2, NULL);
  xTaskCreate(taskDHT, "DHT", 2048, NULL, 1, NULL);
  xTaskCreate(taskTilt, "Tilt", 2048, NULL, 3, NULL);
  xTaskCreate(taskUnlock, "Unlock", 4096, NULL, 2, NULL);
  xTaskCreate(taskAutoLock, "AutoLock", 2048, NULL, 1, NULL);
  
  Serial.println("=== Systeme pret ===\n");
}

void loop() {
  server.handleClient();
  vTaskDelay(10 / portTICK_PERIOD_MS);
}

// Fonctions Buzzer
void beepSuccess() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
}

void beepFailure() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    delay(150);
  }
}

void beepBlocked() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(1000);
  digitalWrite(BUZZER_PIN, LOW);
}

// Gestion des échecs
bool checkAndUpdateFailures(LockMethod method) {
  if (millis() < failures[method].blockUntil) {
    return false;
  }
  return true;
}

void recordFailure(LockMethod method) {
  failures[method].count++;
  
  if (failures[method].count == 1 || failures[method].count == 2) {
    beepFailure();
    Serial.printf("Echec tentative %d/%d\n", failures[method].count, 3);
  } else if (failures[method].count >= 3) {
    failures[method].blockUntil = millis() + 30000;
    Serial.println("!!! BLOQUE 30 SECONDES !!!");
    beepBlocked();
    vTaskDelay(3000 / portTICK_PERIOD_MS);
  }
}

void resetFailures(LockMethod method) {
  failures[method].count = 0;
  failures[method].blockUntil = 0;
}

void unlockDoor() {
  if (xSemaphoreTake(servoMutex, portMAX_DELAY)) {
    Serial.println("DEVERROUILLAGE AUTORISE");
    doorServo.write(UNLOCKED_ANGLE);
    doorOpen = true;
    beepSuccess();
    xSemaphoreGive(servoMutex);
  }
}

void lockDoor() {
  if (xSemaphoreTake(servoMutex, portMAX_DELAY)) {
    Serial.println("Verrouillage automatique");
    doorServo.write(LOCKED_ANGLE);
    doorOpen = false;
    xSemaphoreGive(servoMutex);
  }
}

// Serveur Web - Page d'accueil
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Serrure Intelligente</title>";
  html += "<style>body{font-family:Arial;text-align:center;padding:20px;}";
  html += "input{padding:10px;margin:10px;font-size:18px;}";
  html += "button{padding:15px 30px;font-size:18px;background:#4CAF50;color:white;border:none;cursor:pointer;}";
  html += "button:hover{background:#45a049;}</style></head><body>";
  html += "<h1>Serrure Intelligente ESP32</h1>";
  html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
  html += "<form action='/unlock' method='GET'>";
  html += "<input type='password' name='password' placeholder='Mot de passe' required>";
  html += "<br><button type='submit'>Deverrouiller</button>";
  html += "</form></body></html>";
  
  server.send(200, "text/html", html);
}

void handleNotFound() {
  String message = "Page non trouvee\n\n";
  message += "URI: " + server.uri() + "\n";
  message += "Methode: ";
  message += (server.method() == HTTP_GET ? "GET" : "POST");
  message += "\n";
  message += "Essayez: http://" + WiFi.localIP().toString() + "/unlock?password=237";
  server.send(404, "text/plain", message);
  Serial.println("404 - " + server.uri());
}

void handleWebUnlock() {
  Serial.println("Requete WiFi recue");
  
  if (!checkAndUpdateFailures(WIFI)) {
    server.send(403, "text/html", "<html><body><h1>Bloque 30 secondes</h1></body></html>");
    return;
  }
  
  if (server.hasArg("password")) {
    String pass = server.arg("password");
    Serial.println("Mot de passe recu: " + pass);
    
    if (pass == WIFI_BT_PASSWORD) {
      resetFailures(WIFI);
      LockMethod method = WIFI;
      xQueueSend(unlockQueue, &method, 0);
      server.send(200, "text/html", "<html><body><h1>Deverrouille!</h1></body></html>");
      return;
    } else {
      recordFailure(WIFI);
      server.send(401, "text/html", "<html><body><h1>Mot de passe incorrect</h1></body></html>");
      return;
    }
  }
  
  server.send(400, "text/html", "<html><body><h1>Parametre manquant</h1></body></html>");
}

// Tâche Clavier
void taskKeypad(void *param) {
  String input = "";
  
  while (1) {
    char key = keypad.getKey();
    if (key) {
      Serial.println("Touche: " + String(key));
      if (key == '#') {
        if (checkAndUpdateFailures(KEYPAD)) {
          if (input == KEYPAD_PASSWORD) {
            resetFailures(KEYPAD);
            LockMethod method = KEYPAD;
            xQueueSend(unlockQueue, &method, 0);
          } else {
            recordFailure(KEYPAD);
          }
        }
        input = "";
      } else if (key == '*') {
        input = "";
      } else {
        input += key;
      }
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// Tâche RFID
void taskRFID(void *param) {
  while (1) {
    if (!checkAndUpdateFailures(RFID)) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }
    
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      Serial.print("RFID detecte: ");
      for (byte i = 0; i < 4; i++) {
        Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
        Serial.print(mfrc522.uid.uidByte[i], HEX);
      }
      Serial.println();
      
      bool valid = false;
      for (int i = 0; i < 2; i++) {
        if (memcmp(mfrc522.uid.uidByte, validUIDs[i], 4) == 0) {
          valid = true;
          break;
        }
      }
      
      if (valid) {
        resetFailures(RFID);
        LockMethod method = RFID;
        xQueueSend(unlockQueue, &method, 0);
      } else {
        recordFailure(RFID);
      }
      
      mfrc522.PICC_HaltA();
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Tâche IR
void taskIR(void *param) {
  while (1) {
    if (!checkAndUpdateFailures(IR)) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }
    
    if (IrReceiver.decode()) {
      Serial.printf("IR recu: 0x%08X\n", IrReceiver.decodedIRData.decodedRawData);
      
      if (IrReceiver.decodedIRData.decodedRawData == IR_VALID_CODE) {
        resetFailures(IR);
        LockMethod method = IR;
        xQueueSend(unlockQueue, &method, 0);
      } else {
        recordFailure(IR);
      }
      IrReceiver.resume();
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Tâche Empreinte
void taskFingerprint(void *param) {
  while (1) {
    if (!checkAndUpdateFailures(FINGERPRINT)) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }
    
    if (finger.getImage() == FINGERPRINT_OK) {
      if (finger.image2Tz() == FINGERPRINT_OK) {
        if (finger.fingerSearch() == FINGERPRINT_OK) {
          Serial.println("Empreinte reconnue!");
          resetFailures(FINGERPRINT);
          LockMethod method = FINGERPRINT;
          xQueueSend(unlockQueue, &method, 0);
        } else {
          recordFailure(FINGERPRINT);
        }
      }
    }
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

// Tâche Bluetooth
void taskBluetooth(void *param) {
  String btInput = "";
  
  while (1) {
    if (SerialBT.available()) {
      char c = SerialBT.read();
      
      if (c == '\n' || c == '\r') {
        btInput.trim();
        if (btInput.length() > 0) {
          Serial.println("Mot de passe BT recu: [" + btInput + "]");
          
          if (checkAndUpdateFailures(BLUETOOTH)) {
            if (btInput == WIFI_BT_PASSWORD) {
              resetFailures(BLUETOOTH);
              LockMethod method = BLUETOOTH;
              xQueueSend(unlockQueue, &method, 0);
              SerialBT.println("OK");
              Serial.println("Bluetooth: Acces autorise");
            } else {
              recordFailure(BLUETOOTH);
              SerialBT.println("ERREUR");
              Serial.println("Bluetooth: Mot de passe incorrect");
            }
          } else {
            SerialBT.println("BLOQUE");
            Serial.println("Bluetooth: Bloque 30s");
          }
        }
        btInput = "";
      } else if (c >= 32 && c <= 126) {
        btInput += c;
      }
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// Tâche DHT
void taskDHT(void *param) {
  while (1) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    
    if (isnan(h) || isnan(t)) {
      Serial.println("Erreur lecture DHT11");
    } else {
      temperature = t;
      humidity = h;
      Serial.print("Temperature: ");
      Serial.print(t, 1);
      Serial.print("C | Humidite: ");
      Serial.print(h, 1);
      Serial.println("%");
    }
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

// Tâche Tilt
void taskTilt(void *param) {
  bool tiltDetected = false;
  
  while (1) {
    int tiltState = digitalRead(TILT_PIN);
    
    if (tiltState == LOW) {
      if (!tiltDetected) {
        Serial.println("ALERTE: Porte brutalisee!");
        beepBlocked(); // Bip long pour signaler la brutalisation
        tiltDetected = true;
      }
      vTaskDelay(100 / portTICK_PERIOD_MS);
    } else {
      if (tiltDetected) {
        Serial.println("Tilt switch revenu a la normale");
        tiltDetected = false;
      }
      vTaskDelay(100 / portTICK_PERIOD_MS);
    }
  }
}

// Tâche Déverrouillage
void taskUnlock(void *param) {
  LockMethod method;
  
  while (1) {
    if (xQueueReceive(unlockQueue, &method, portMAX_DELAY)) {
      unlockDoor();
    }
  }
}

// Tâche Verrouillage Auto
void taskAutoLock(void *param) {
  unsigned long unlockTime = 0;
  
  while (1) {
    if (doorOpen && unlockTime == 0) {
      unlockTime = millis();
    }
    
    if (doorOpen && (millis() - unlockTime >= 5000)) {
      lockDoor();
      unlockTime = 0;
    }
    
    if (!doorOpen) {
      unlockTime = 0;
    }
    
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}