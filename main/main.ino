#include <SoftwareSerial.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>

#define DEBUG

#ifdef DEBUG
  const char ssid[] = "Taohuu_2.4G";
  const char pass[] = "Huu12345";
#else
  const char ssid[] = "@JumboPlusIoT";
  const char pass[] = "f0b8ql89";
#endif

#define LED 15
#define RELAY 19

enum COLOR_STATUS {
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  INVALID_QR,
  VALID_QR,
  SYSTEM_FAILED,
  CLEAR
};

const String host = "http://192.168.1.8:9000";
const String apiUrl = host + "/api/device/scanner/";

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(8, LED, NEO_GRB + NEO_KHZ800);
SoftwareSerial mySerial(17, 16); // TX, RX
WiFiClient net; // create wifi object

bool onProcess = false;

void connect() {
  setLED(WIFI_CONNECTING);
  Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  setLED(WIFI_CONNECTED);
  Serial.println("\nwifi connected!");
  delay(3000);
  setLED(CLEAR);
}

void setup() {
  // put your setup code here, to run once:
  mySerial.begin(9600); // set the data rate for the SoftwareSerial port
  Serial.begin(19200);
  WiFi.begin(ssid, pass);
  pinMode(RELAY,OUTPUT);
  pixels.begin();
  pixels.show();
  connect();
}

void loop() {
  // put your main code here, to run repeatedly:
  if (WiFi.status() != WL_CONNECTED) {
    connect();
  }
  if (mySerial.available() && !onProcess) // Check if there is Incoming Data in the Serial Buffer.
  {
    String qrCode = mySerial.readString();
    handleQRCode(qrCode);
  }
}

void handleQRCode(String qrCode){
  // set on process qrcode
  onProcess = true;

  // QR Code String prepare
  int index = qrCode.indexOf(':');
  int length = qrCode.length();
  if(!qrCode.substring(0, index).equals("QRTHROUGH")){
    setLED(INVALID_QR);
    delay(3000);
    setLED(CLEAR);
    onProcess = false;
    return;
  }
  String qrSpilted = qrCode.substring(index+1, length-1);
  index = qrSpilted.indexOf("QRTHROUGH:");
  if(index >= 0){
    qrSpilted = qrSpilted.substring(0,index-1);
  }

  if(WiFi.status()== WL_CONNECTED){
    HTTPClient http;

    String apiRequest = apiUrl + qrSpilted;
    http.begin(apiRequest.c_str());

    int httpResponseCode = http.GET();
    // if (httpResponseCode>0) {
    //     Serial.print("HTTP Response code: ");
    //     Serial.println(httpResponseCode);
    //     String payload = http.getString();
    //     Serial.println(payload);
    //   }
    //   else {
    //     Serial.print("Error code: ");
    //     Serial.println(httpResponseCode);
    //   }
    //   // Free resources
    //   http.end();
    if(httpResponseCode == 200){
      String payload = http.getString();
      JSONVar payloadObject = JSON.parse(payload);

      bool success = payloadObject["success"];
      if(success){
        doorService();
      }
    }else{
      switch(httpResponseCode){
        case 401: 
          setLED(INVALID_QR);
          delay(3000);
          setLED(CLEAR);
          break;
        default:
          setLED(SYSTEM_FAILED);
          delay(3000);
          setLED(CLEAR);
          break;
      }
    }
    http.end();
  }

  onProcess = false;
}

void doorService(){
  setLED(VALID_QR);
  digitalWrite(RELAY,HIGH);
  delay(3000);
  digitalWrite(RELAY,LOW);
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
      case WIFI_CONNECTED:
        R = 0;
        G = 0;
        B = 255;
        break;
      case INVALID_QR:
        R = 255;
        G = 0;
        B = 0;
        break;
      case VALID_QR:
        R = 0;
        G = 255;
        B = 0;
        break;
      case SYSTEM_FAILED:
        R = 255;
        G = 69;
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
