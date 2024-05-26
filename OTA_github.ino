#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include "ArduinoJson.h"
#include <MQTTClient.h>
#include "secret.h"

//Release modification

#define BOARD NODE01
const int currentVersion = 10;

#if BOARD == ESP01
const char* board = "esp01";
#define AWS_IOT_PUBLISH_TOPIC   "esp01/pub"
#define DEBUG_ERROR false
#define DEBUG_WARNING false
#define DEBUG_INFO false
#define DEBUG_NOTICE false
#elif BOARD == NODE01
const char* board = "node01";
#define AWS_IOT_PUBLISH_TOPIC   "node01/pub"
#define DEBUG_ERROR false
#define DEBUG_WARNING true
#define DEBUG_INFO true
#define DEBUG_NOTICE false
#endif

//#define AWS_IOT_SUBSCRIBE_TOPIC "node01/sub"
#define PUBLISH_INTERVAL 60000  // 60 seconds, 0 to 4,294,967,295
#define LED_INTERVAL_OFF 10000  // 10 seconds, 0 to 4,294,967,295
#define LED_INTERVAL_ON 50  // 0.05 seconds, 0 to 4,294,967,295
#define FIRMWARE_INTERVAL 60000// 60 seconds, 0 to 4,294,967,295
 
BearSSL::WiFiClientSecure network;
BearSSL::X509List x509(AWS_CERT_CA);
BearSSL::X509List x509_crt(AWS_CERT_CRT);
BearSSL::PrivateKey prv_key(AWS_CERT_PRIVATE);

MQTTClient client_aws = MQTTClient(256);
time_t now;
time_t nowish = 1510592825;


#define DEBUG_E if(DEBUG_ERROR)Serial
#define DEBUG_W if(DEBUG_WARNING)Serial
#define DEBUG_I if(DEBUG_INFO)Serial
#define DEBUG_N if(DEBUG_NOTICE)Serial
const char* ssid = "Hoang Anh";
const char* password = "999999999";
//const char* ssid = "Redmi Note 11";
//const char* password = "hoanganh";


const char* serverPath = "http://blog.ezcodea.biz/";
unsigned long timerLed = 0; //variable to subtract from millis()
unsigned long timerSendMess= 0;
unsigned long timerFirmCheck = 0;
unsigned long lastPublishTime = 0;
void setup() {
  int cnt = 50;
#if DEBUG_ERROR == true || DEBUG_WARNING == true || DEBUG_INFO == true || DEBUG_NOTICE == true
  Serial.begin(115200);
#endif
  DEBUG_I.println("[INFO] Starting up the MCU.");
  DEBUG_I.print("[INFO] ESP8266 http update, current version: ");
  DEBUG_I.println(currentVersion);
  WiFi.begin(ssid, password);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DEBUG_I.print(".");
    if( cnt == 0){
      DEBUG_I.println("Reset..");
      ESP.restart();
    }
    cnt--;
  }
  digitalWrite(LED_BUILTIN, HIGH);
  NTPConnect();
  connectAWS();
}
void loop() {
  // Send an HTTP POST request depending on timerDelay
  unsigned char wifi_status = 0;
  if (millis() - timerLed > LED_INTERVAL_OFF) {
    digitalWrite(LED_BUILTIN, LOW);
    unsigned int heap = ESP.getFreeHeap();
    DEBUG_I.print("[INFO] Heap: ");
    DEBUG_I.println(heap);
    timerLed = millis(); //setting "timer = millis" makes millis-timer = 0
  }
  else if (millis() - timerLed > LED_INTERVAL_ON) {
    if (!wifi_status) digitalWrite(LED_BUILTIN, HIGH);
  }
  if (millis() - timerSendMess > PUBLISH_INTERVAL) {
    connectAWS();
    publishMessage();
    timerSendMess = millis(); //setting "timer = millis" makes millis-timer = 0
  }
      
  if ((millis() - timerFirmCheck) > FIRMWARE_INTERVAL) 
  {
    //Check WiFi connection status
    if(WiFi.status() == WL_CONNECTED)
    {
      WiFiClient client;
      HTTPClient http;
      DynamicJsonDocument doc(200);
      String link_bin;
      int version = 0;
      wifi_status = 1;

      
      // Your Domain name with URL path or IP address with path
      http.begin(client, serverPath);
      
      int httpResponseCode = http.GET();

      if (httpResponseCode>0) {
        String link_board = "link_";
        DEBUG_I.print("\n[INFO] Current version: ");
        DEBUG_I.println(currentVersion);
        DEBUG_I.print("[INFO] Get version available: ");
        String payload = http.getString();
        
        deserializeJson(doc, payload);
        version = ((int)doc["version"]);
        link_board = link_board + board;
        link_bin = doc[link_board].as<String>();
        DEBUG_I.println(version);
        DEBUG_I.print("[INFO] Link BIN file: ");
        DEBUG_I.println(link_bin);
      }
      else 
      {
        DEBUG_W.print("[WARN] Return code: ");
        DEBUG_W.println(httpResponseCode);
      }
      // Free resources
      http.end();
      if(version > currentVersion)
      {
        DEBUG_I.println("[INFO] Upgrading device...");
        digitalWrite(LED_BUILTIN, LOW);
        t_httpUpdate_return ret = ESPhttpUpdate.update(client, link_bin, "1.0");
        switch (ret) 
		    {
          case HTTP_UPDATE_FAILED:
            DEBUG_E.printf("[ERROR] HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(),
                          ESPhttpUpdate.getLastErrorString().c_str());
            break;
          case HTTP_UPDATE_NO_UPDATES:
            DEBUG_I.println("[INFO] HTTP_UPDATE_NO_UPDATES");
            break;
          case HTTP_UPDATE_OK:
            DEBUG_I.println("[INFO] HTTP_UPDATE_OK");
            break;
		    }
	    }
      else
      {
        DEBUG_I.println("[INFO] Firmware latest has been used.");
      }
    }
    else 
    {
      DEBUG_W.println("[WARN] WiFi Disconnected");
      wifi_status = 0;
    }
    timerFirmCheck = millis();
  }

}

void NTPConnect(void)
{
  DEBUG_I.print("[INFO] Setting time using SNTP");
  configTime(TIME_ZONE * 3600, 0 * 3600, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);
  while (now < nowish)
  {
    delay(500);
    DEBUG_I.print(".");
    now = time(nullptr);
  }
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  DEBUG_I.print("[INFO] Current time: ");
  DEBUG_I.println(asctime(&timeinfo));
}

void connectAWS()
{
  network.setTrustAnchors(&x509);
  network.setClientRSACert(&x509_crt, &prv_key);
  // Connect to the MQTT broker on the AWS endpoint we defined earlier
  client_aws.begin(AWS_IOT_ENDPOINT, 8883, network);
  DEBUG_I.println("[INFO] Connecting to AWS IOT");
 
  while (!client_aws.connect(THINGNAME))
  {
    DEBUG_I.print(".");
    delay(1000);
  }
 
  if (!client_aws.connected()) {
    DEBUG_W.println("[WARN] AWS IoT Timeout!");
    return;
  }
  // Subscribe to a topic, not need to use sub now
  //client_aws.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
 
  DEBUG_I.println("[INFO] AWS IoT Connected!");
}

void publishMessage()
{
  StaticJsonDocument<200> doc;
  doc["version"] = currentVersion;
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer); // print to client
  client_aws.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
  DEBUG_I.println("[INFO] Send message to AWS OK!");
  network.stop();
  client_aws.disconnect();
}