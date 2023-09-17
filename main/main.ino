#include <SoftwareSerial.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <MQTT.h>
#include <WebServer.h>
#include <Update.h>

WebServer server(80);
#define DEBUG
#define VERSION_MQTT

#ifdef DEBUG
  const char ssid[] = "Taohuu_2.4G";
  const char pass[] = "Huu12345";
#else
  const char ssid[] = "@JumboPlusIoT";
  const char pass[] = "0jycjtfn";
  // const char ssid[] = "@JumboPlusIoT";
  // const char pass[] = "trqzz0kt";
#endif

const char mqtt_broker[]="45.136.237.10";
const char mqtt_topic[]="monitor";
const char mqtt_client_id[]="qr_through_baan_nc"; // must change this string to a unique value
const char mqtt_user[]="qrthrough";
const char mqtt_pass[]="arnan1234";
int MQTT_PORT=1883;

#define LED 23
#define RELAY 19
#define RESET_DISCONNECTED 30 * 60 * 1000

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

const char* loginIndex = 
 "<form name='loginForm'>"
    "<table width='20%' bgcolor='A09F9F' align='center'>"
        "<tr>"
            "<td colspan=2>"
                "<center><font size=4><b>QRThrough ESP32(Baan.NC) Login Page</b></font></center>"
                "<br>"
            "</td>"
            "<br>"
            "<br>"
        "</tr>"
        "<td>Username:</td>"
        "<td><input type='text' size=25 name='userid'><br></td>"
        "</tr>"
        "<br>"
        "<br>"
        "<tr>"
            "<td>Password:</td>"
            "<td><input type='Password' size=25 name='pwd'><br></td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
            "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
        "</tr>"
    "</table>"
"</form>"
"<script>"
    "function check(form)"
    "{"
    "if(form.userid.value=='qrthrough' && form.pwd.value=='arnan1234')"
    "{"
    "window.open('/serverIndex')"
    "}"
    "else"
    "{"
    " alert('Error Password or Username')/*displays error message*/"
    "}"
    "}"
"</script>";
 

// หน้า Index Page
const char* serverIndex = 
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "let pass = prompt('Please enter your password');"
  "if(pass === 'arnan1234')"
    "{"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')" 
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
  "}"
    "else"
    "{"
    " alert('Error Password mismatched')/*displays error message*/"
    "}"
 "});"
 "</script>";

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
  webSetup();
}

void loop() {

  server.handleClient();
  delay(1);

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
    client.publish(mqtt_topic, "STATUS | DOOR MODE : " + doorModeToString(DOOR_STATUS) + " | SCANNER ACTIVE : "  + (SCANNER_ACTIVE ? "TRUE" : "FALSE") + " | API ACTIVE : "  + (SERVER_ACTIVE ? "TRUE" : "FALSE") + " | LOCAL ID : " + WiFi.localIP().toString());
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

void connect() {
  WIFIconnect();
  #ifdef VERSION_MQTT
    MQTTconnect();
  #endif
  APIconnect();
  ledService(ALL_CONNECTED);
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

void MQTTconnect(){
  setLED(INVALID_QR);
  Serial.print("\nconnecting MQTT...");
  while (!client.connect(mqtt_client_id,mqtt_user,mqtt_pass)) {  
    Serial.print(".");
    delay(1000);
    if (WiFi.status() != WL_CONNECTED) {
      WIFIconnect();
    }
  }
  client.subscribe("door_manager");
  client.subscribe("controller");
  client.subscribe("status");
  Serial.println("\nMQTT connected!");
  setLED(CLEAR);
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

  if(topic == "controller"){
    if(payload == "reset") ESP.restart();
  }

  if(topic == "status"){
    publishStatus = true;
  }
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

void webSetup(){
  // แสดงหน้า Server Index หลังจาก Login
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", loginIndex);
  });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  
// ขั้นตอนการ Upload ไฟล์
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Serial.printf("Error 1");
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {

// แฟลช(เบิร์นโปรแกรม)ลง ESP32
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  server.begin();
  Serial.println("WebOTA Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
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

String doorModeToString(DOOR_MODE mode) {
  switch (mode) {
    case ON:
      return "ON";
    case OFF:
      return "OFF";
    case AUTO:
      return "AUTO";
    default:
      return "UNKNOWN"; // Handle unknown enum values
  }
}