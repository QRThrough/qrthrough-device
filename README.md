# QRThrough: Scanner Access Control

**<p style ="text-align: center;">A hardware project for access control system using QR code scanner and ESP32 micro-controller.</p>**

## Description

The project is a part of the QRThrough system, which is a system for managing access control using QR code scanner. The system consists of three main parts: the scanner-access-control, front-end, and back-end. The scanner-access-control is a hardware device that is used to scan the QR code and control the access of the door. The front-end is a web application that is used to register the user and manage the system by the moderator. The back-end is a server that is used to store the data, generate the QR code, handle one-time password, connect with [LINE](https://line.me/th/) chat via [LINE Messaging API](https://developers.line.biz/en/services/messaging-api/), and manage the system.

## Components

- ESP32-DevKitC V4
- Embedded 2d scanner platform
- NeoPixels RGB LED strip bar
- Relay module 5V low-trigger

## Getting Started

To develop the scanner-access-control, you need to install the Arduino IDE and the ESP32 board package. You can follow the instructions below to install the Arduino IDE and the ESP32 board package.

### Prerequisites

The first instruction is to install the Arduino IDE. You can download the Arduino IDE from the [official website](https://www.arduino.cc/en/software). After downloading the Arduino IDE, you can install it by following the instructions on the website.

The second instruction is to install the ESP32 board package. You can follow the instructions with this link [Installing the ESP32 Board in Arduino IDE (Windows, Mac OS X, Linux)](https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/).

### Packages

The scanner-access-control uses the following libraries:

- SoftwareSerial
- Adafruit_NeoPixel
- WiFi
- HTTPClient
- ArduinoJson
- MQTT
- WebServer
- Update

### Setup

The scanner-access-control consists of two main parts: the setup and the loop. The setup is the part where the device starts working. The setup consists of the following parts:

- System configuration

```
WebServer server(80); // ตั้งค่า port เว็บสำหรับ OTA อัพเดต patch โปรแกรมของ module นี้
//#define DEBUG // กำหนดโหมด DEBUG
#define VERSION_MQTT // กำหนดใช้งานเวอร์ชั่น MQTT

// ตั้งค่าการเชื่อมต่อ WiFi
#ifdef DEBUG
  const char ssid[] = "";
  const char pass[] = "";
#else
  const char ssid[] = "@JumboPlusIoT"; // กำหนด username สำหรับเชื่อมต่อ WiFi
  const char pass[] = "trqzz0kt"; // กำหนด password สำหรับเชื่อมต่อ WiFi
#endif

// ตั้งค่าการเชื่อมต่อ MQTT
const char mqtt_broker[]="45.136.237.10";
const char mqtt_topic[]="monitor";
const char mqtt_client_id[]="qr_through_baan_nc";
const char mqtt_user[]="qrthrough";
const char mqtt_pass[]="arnan1234";
int MQTT_PORT=1883;

#define LED 23 // กำหนด PIN ของ LED stripe
#define RELAY 19 // กำหนด PIN ของ Relay
#define RESET_DISCONNECTED 30 * 60 * 1000 // กำหนดเวลารีเซ็ทเครื่องหากขาดการเชื่อมต่อ WiFi

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(8, LED, NEO_GRB + NEO_KHZ800);
SoftwareSerial mySerial(17, 16); // กำหนด PIN ของ TX, RX สำหรับสื่อสารกับเครื่องอ่าน QR code
```

- End-point API configuration

```
  const String host = "https://api.qr-through.com/api/";
  const String apiUrl = host + "device/scanner/";
```

- Setup process

```
void setup() {
  Serial.begin(115200); // เปิด Serial port สำหรับ debug
  mySerial.begin(9600); // เปิด Serial port สำหรับสื่อสารกับเครื่องอ่าน QR code
  WiFi.begin(ssid, pass); // เชื่อมต่อ WiFi
  pinMode(RELAY, OUTPUT); // กำหนด PIN ของ Relay เป็น OUTPUT
  pixels.begin(); // เปิดใช้งาน LED stripe
  pixels.show(); // แสดงสีของ LED stripe
  #ifdef VERSION_MQTT
    client.begin(mqtt_broker, MQTT_PORT, net); // เชื่อมต่อ MQTT
    client.onMessage(messageReceived); // กำหนดฟังก์ชันที่จะทำงานเมื่อมีข้อความเข้ามา
  #endif
  connect(); // procedure ตรวจสอบการเชื่อมต่อต่างๆ
  webSetup(); // procedure ตั้งค่าเว็บเซิร์ฟเวอร์สำหรับ OTA อัพเดต patch โปรแกรมของ module นี้
}
```

The loop is the part where the device works continuously. The loop consists of the following parts:

- Loop process

```
void loop() {

  server.handleClient(); // ตรวจสอบการเชื่อมต่อเว็บเซิร์ฟเวอร์
  delay(1);

  if (WiFi.status() != WL_CONNECTED) { // ตรวจสอบการเชื่อมต่อ WiFi
    connect(); // procedure ตรวจสอบการเชื่อมต่อต่างๆ
  }

  #ifdef VERSION_MQTT
  client.loop(); // ตรวจสอบการเชื่อมต่อ MQTT
  delay(10);
  if (!client.connected()) { // หากเชื่อมต่อ MQTT ไม่สำเร็จ
    MQTTconnect(); // procedure เชื่อมต่อ MQTT
  }
  #endif

  if(!SERVER_ACTIVE) { // หากเชื่อมต่อ API ไม่สำเร็จ
    APIconnect(); // procedure เชื่อมต่อ API
  }

  #ifdef VERSION_MQTT
  if(publishStatus){ // หากต้องการส่งข้อมูลสถานะ
    client.publish(mqtt_topic, "STATUS | DOOR MODE : " + doorModeToString(DOOR_STATUS) + " | SCANNER ACTIVE : "  + (SCANNER_ACTIVE ? "TRUE" : "FALSE") + " | API ACTIVE : "  + (SERVER_ACTIVE ? "TRUE" : "FALSE") + " | LOCAL ID : " + WiFi.localIP().toString()); // ส่งข้อมูลสถานะ
    publishStatus = false; // กำหนดให้ไม่ส่งข้อมูลสถานะ
  }
  #endif

  if (mySerial.available() && !onProcess && SCANNER_ACTIVE) // ตรวจสอบข้อมูลที่รับเข้ามาจากเครื่องอ่าน QR code
  {
    String qrCode = mySerial.readString(); // อ่านข้อมูล QR code
    handleQRCode(qrCode); // procedure ตรวจสอบข้อมูล QR code
  }
}
```

## Functions and Procedures specification

The scanner-access-control consists of the following functions and procedures:

| Function/Procedure | Description                                             | Parameters                     | Return |
| ------------------ | ------------------------------------------------------- | ------------------------------ | ------ |
| connect()          | Check the connection status of the WiFi, MQTT, and API. | None                           | None   |
| WIFIconnect()      | Handle the connection status of the WiFi.               | None                           | None   |
| MQTTconnect()      | Handle the connection status of the MQTT.               | None                           | None   |
| messageReceived()  | Handle the message received from the MQTT.              | topic(String), payload(String) | None   |
| APIconnect()       | Handle the connection status of the API.                | None                           | None   |
| checkHealthAPI()   | Check the health status of the API.                     | None                           | None   |
| webSetup()         | Setup the web server for OTA update.                    | None                           | None   |
| handleQRCode()     | Handle the QR code data.                                | qrCode(String)                 | None   |
| doorService()      | Control the door service to open                        | None                           | None   |
| ledService()       | Control the LED service to show the status              | status(COLOR_STATUS)           | None   |
| setLED()           | Set the color of the LED stripe.                        | status(COLOR_STATUS)           | None   |
| doorModeToString() | Convert the door mode to string.                        | doorMode(DOOR_STATUS)          | String |

## Deployment

The scanner-access-control is deployed by uploading the code to the ESP32 micro-controller. You can follow the instructions below to upload the code to the ESP32 micro-controller.

### Uploading

The first instruction is to connect the ESP32 micro-controller to the computer using the USB cable. The second instruction is to select the board and port in the Arduino IDE. You can follow the instructions with this link [Installing the ESP32 Board in Arduino IDE (Windows, Mac OS X, Linux)](https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/). The third instruction is to upload the code to the ESP32 micro-controller. You can upload the code by clicking the upload button in the Arduino IDE.

### Over-the-air

The scanner-access-control can be updated over-the-air (OTA) by using the web server. You can follow the instructions below to update the scanner-access-control over-the-air.

1. Open the web browser and go to the IP address of the scanner-access-control.

2. Login to the web server by using the username and password.

3. Click the "Choose File" button and select the firmware file.

4. Click the "Update" button to update the firmware and enter the password.

5. Wait for the scanner-access-control to update the firmware, If the process is reached 100%, the scanner-access-control will restart and the firmware will be updated.

## Authors

- [JM Jirapat](https://github.com/JMjirapat) - Developer
