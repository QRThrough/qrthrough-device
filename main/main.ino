#include <SoftwareSerial.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <ArduinoOTA.h>
#include <MQTT.h>

//#define DEBUG
//#define VERSION_MQTT

#ifdef DEBUG
  const char ssid[] = "Taohuu_2.4G";
  const char pass[] = "Huu12345";
#else
  const char ssid[] = "@JumboPlusIoT";
  const char pass[] = "0jycjtfn";
  // const char ssid[] = "@JumboPlusIoT";
  // const char pass[] = "trqzz0kt";
#endif

const char mqtt_broker[]="test.mosquitto.org";
const char mqtt_topic[]="monitor";
const char mqtt_client_id[]="qr_through_baan_nc"; // must change this string to a unique value
int MQTT_PORT=1883;

#define LED 23
#define RELAY 19
#define RESET_DISCONNECTED 60 * 60 * 1000

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(8, LED, NEO_GRB + NEO_KHZ800);
SoftwareSerial mySerial(17, 16); // TX, RX
WiFiClient net; // create wifi object
MQTTClient client;

enum COLOR_STATUS {
  WIFI_CONNECTING,
  ALL_CONNECTED,
  INVALID_QR,
  VALID_QR,
  SYSTEM_FAILED,
  CLEAR
};

enum DOOR_MODE {
  ON,
  OFF,
  AUTO,
};

const String host = "https://api.qr-through.com/api/";
const String apiUrl = host + "device/scanner/";

bool SCANNER_ACTIVE = true;
DOOR_MODE DOOR_STATUS = AUTO;
bool SERVER_ACTIVE = false;
bool publishStatus = false;
bool onProcess = false;
uint countLoss = 0;

void connect() {
  WIFIconnect();
  #ifdef VERSION_MQTT
    MQTTconnect();
  #endif
  APIconnect();
  ledService(ALL_CONNECTED);
}

void messageReceived(String &topic, String &payload) {
  Serial.println("incoming: " + topic + " - " + payload);

  if(topic == "door_manager"){
    if(payload == "on"){
      SCANNER_ACTIVE = false;
      DOOR_STATUS = ON;
      digitalWrite(RELAY,HIGH);
    }else if(payload == "off"){
      SCANNER_ACTIVE = false;
      DOOR_STATUS = OFF;
      digitalWrite(RELAY,LOW);
    }else{
      SCANNER_ACTIVE = true;
      DOOR_STATUS = AUTO;
      digitalWrite(RELAY,LOW);
    }
  }

  if(topic == "scanner_manager"){
    if(payload == "off"){
      SCANNER_ACTIVE = false;
    }else{
      SCANNER_ACTIVE = true;
    }
  }

  if(topic == "status"){
    publishStatus = true;
  }
  // Note: Do not use the client in the callback to publish, subscribe or
  // unsubscribe as it may cause deadlocks when other things arrive while
  // sending and receiving acknowledgments. Instead, change a global variable,
  // or push to a queue and handle it in the loop after calling `client.loop()`.
}

void setup() {
  // put your setup code here, to run once:
  mySerial.begin(9600); // set the data rate for the SoftwareSerial port
  Serial.begin(9600);
  WiFi.begin(ssid, pass);
  pinMode(RELAY,OUTPUT);
  pixels.begin();
  pixels.show();
  #ifdef VERSION_MQTT
    client.begin(mqtt_broker, MQTT_PORT, net);
    client.onMessage(messageReceived);
  #endif
  connect();
  OTASetup();
}

void loop() {
  ArduinoOTA.handle();

  if (WiFi.status() != WL_CONNECTED) {
    connect();
  }

  #ifdef VERSION_MQTT
  client.loop();
  delay(10);  // <- fixes some issues with WiFi stability
    if (!client.connected()) {
      MQTTconnect();
    }
  #endif

  if(!SERVER_ACTIVE) {
    APIconnect();
  }

  #ifdef VERSION_MQTT
  if(publishStatus){
    client.publish(mqtt_topic, "STATUS | DOOR MODE : " + String(DOOR_STATUS) + " | SCANNER ACTIVE : "  + String(SCANNER_ACTIVE) + " | API ACTIVE : "  + String(SERVER_ACTIVE));
    publishStatus = false;
  }
  #endif

  // put your main code here, to run repeatedly:
  if (mySerial.available() && !onProcess && SCANNER_ACTIVE) // Check if there is Incoming Data in the Serial Buffer.
  {
    String qrCode = mySerial.readString();
    handleQRCode(qrCode);
  }
}

void checkHealthAPI(){
  if(WiFi.status() == WL_CONNECTED){
    HTTPClient http;

    http.begin(host.c_str());

    int httpResponseCode = http.GET();
    if(httpResponseCode == 200){
      String payload = http.getString();
      JSONVar payloadObject = JSON.parse(payload);

      bool success = payloadObject["success"];
      if(success){
        SERVER_ACTIVE = true;
      }
    }
    http.end();
  }else{
    WIFIconnect();
  }
}

void MQTTconnect(){
  setLED(INVALID_QR);
  Serial.print("\nconnecting MQTT...");
  while (!client.connect(mqtt_client_id)) {  
    Serial.print(".");
    delay(1000);
    if (WiFi.status() != WL_CONNECTED) {
      WIFIconnect();
    }
  }
  client.subscribe("door_manager");
  client.subscribe("scanner_manager");
  client.subscribe("status");
  Serial.println("\nMQTT connected!");
  setLED(CLEAR);
}

void APIconnect(){
  setLED(SYSTEM_FAILED);
  Serial.print("\nconnecting API...");
  checkHealthAPI();
  while (!SERVER_ACTIVE) {
    Serial.print(".");
    delay(5000);
    checkHealthAPI();
  }
  Serial.println("\nAPI connected!");
  setLED(CLEAR);
}

void WIFIconnect(){
  setLED(WIFI_CONNECTING);
  Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    if(countLoss >= RESET_DISCONNECTED){
      Serial.println("Reset..");
      ESP.restart();       // reset ESP command
    }
    Serial.print(".");
    countLoss += 1;
    delay(1000);
  }
  countLoss = 0;
  Serial.println("\nwifi connected!");
}

void handleQRCode(String qrCode){
  // set on process qrcode
  onProcess = true;
  // QR Code String prepare
  int index = qrCode.indexOf(':');
  int length = qrCode.length();
  if(!qrCode.substring(0, index).equals("QRTHROUGH")){
    ledService(INVALID_QR);
    onProcess = false;
    return;
  }

  String qrSpilted = qrCode.substring(index+1, length-1);
  index = qrSpilted.indexOf("QRTHROUGH:");
  if(index >= 0){
    qrSpilted = qrSpilted.substring(0,index-1);
  }

  if(WiFi.status() == WL_CONNECTED){
    HTTPClient http;

    String apiRequest = apiUrl + qrSpilted;
    http.begin(apiRequest.c_str());
    http.addHeader("Authorization", WiFi.macAddress());

    int httpResponseCode = http.GET();
    if(httpResponseCode == 200){
      String payload = http.getString();
      JSONVar payloadObject = JSON.parse(payload);

      bool success = payloadObject["success"];
      if(success){
        doorService();
      }else{
        ledService(SYSTEM_FAILED);
      }
    }else{
      switch(httpResponseCode){
        case 401:
          ledService(INVALID_QR);
          break;
        case 500:
          ledService(SYSTEM_FAILED);
          SERVER_ACTIVE = false;
          break;
        case 502:
          ledService(SYSTEM_FAILED);
          SERVER_ACTIVE = false;
          break;
        case 503:
          ledService(SYSTEM_FAILED);
          SERVER_ACTIVE = false;
          break;
        case 504:
          ledService(SYSTEM_FAILED);
          SERVER_ACTIVE = false;
          break;
        default:
          ledService(SYSTEM_FAILED);
          break;
      }
    }
    http.end();
  }

  onProcess = false;
}

void doorService(){
  if(DOOR_STATUS != AUTO) return;
  setLED(VALID_QR);
  digitalWrite(RELAY,HIGH);
  delay(3000);
  digitalWrite(RELAY,LOW);
  setLED(CLEAR);
}

void ledService(COLOR_STATUS status){
  setLED(status);
  delay(3000);
  setLED(CLEAR);
}

void setLED(COLOR_STATUS status){
  int R,G,B = 0;
  if(status == CLEAR)
    pixels.clear();
  else {
    switch(status) {
      case WIFI_CONNECTING:
        R = 255;
        G = 255;
        B = 0;
        break;
      case ALL_CONNECTED:
        R = 0;
        G = 0;
        B = 255;
        break;
      case INVALID_QR:
        R = 255;
        G = 69;
        B = 0;
        break;
      case VALID_QR:
        R = 0;
        G = 255;
        B = 0;
        break;
      case SYSTEM_FAILED:
        R = 255;
        G = 0;
        B = 0;
        break;
      default:
        pixels.clear();
        break;
    }
    for(int x = 0; x < 8; x++){
      pixels.setPixelColor(x, pixels.Color(R, G, B)); // Moderately bright green color.
    }
  }
  pixels.show();
}

void OTASetup(){
//ตั้งค่า Hostname เป็น esp3232-[MAC]
  ArduinoOTA.setHostname("qr-through-bann.nc");
  ArduinoOTA.setPassword("baan.nc");
  

// ส่วนของ OTA
  ArduinoOTA
    .onStart([]() {
      String type;          // ประเภทของ OTA ที่เข้ามา
      if (ArduinoOTA.getCommand() == U_FLASH)         // แบบ U_FLASH
        type = "sketch";
      else          // แบบ U_SPIFFS
        type = "filesystem";

      // NOTE: ถ้าใช้เป็นแบบ SPIFFS อาจใช้คำสั่ง SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })

    // เริ่มทำงาน (รับข้อมูลโปรแกรม) พร้อมแสดงความคืบหน้าทาง Serial Monitor
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })

    // แสดงข้อความต่างๆหากเกิด Error ขึ้น
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  Serial.println("OTA Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}