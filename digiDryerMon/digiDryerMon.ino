// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>  // https://github.com/marvinroger/async-mqtt-client (requires: https://github.com/me-no-dev/ESPAsyncTCP)
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "config.h"           // rename sample_config.h and edit any values needed
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EmonLib.h>

// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// Input Pins                                                    //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
#define currentPin      A0
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// Output Pins                                                   //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
#define intLED1Pin  D4  // D4 Wemos Mini D1 and NodeMCU
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// Verb/State Conversions                                        //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
#define LEDon       LOW
#define LEDoff      HIGH
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// Poll Times / Misc                                             //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
#define sctPoll        10  // 10 seconds or more is recommended 
#define rePushPoll     300 // push all sensor data every xx seconds - helpful if retain isn't used as this sketch only pushes data if a sensor changes.
#define mqttQOSLevel   2
#define emonCalcIrms   15000  // current calibration settings
#define emonCurrent    30     // current calibration settings
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// VARS Begin                                                    //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
char mcuHostName[64]; 
char sctTopic[96];    
char lwtTopic[96];  
char buildTopic[96];
char rssiTopic[96];
char sSCTcur[5];
char sSCTcurTemp[5]; 
unsigned long wifiLoopNow = 0;
int mqttTryCount = 0;

float sctDiff = 0.1;  // difference in current to trigger a MQTT publish
// -+-+
float sctCur = 99;
float sctNewCur;
// -+-+
bool initBoot = true;

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
#ifdef DEBUGTELNET
  WiFiServer telnetServer(23);
  WiFiClient telnetClient;
#endif

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
Ticker sctTick;
Ticker rePushTick;
Ticker led1FlipTick;
Ticker wifiReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
EnergyMonitor emon1;

// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// connectToWifi                                                 //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
void connectToWifi() {
  debugLn(F("WIFI: Attempting to Connect."));
  wifiLoopNow = millis(); // mark start time
  WiFi.mode(WIFI_STA);
  WiFi.hostname(mcuHostName);
  WiFi.begin(wifiSSID, wifiPass);
  delay(10);  
  // toggle on board LED as WiFi comes up
  digitalWrite(intLED1Pin, LEDoff);
  while (WiFi.status() != WL_CONNECTED) {
     digitalWrite(intLED1Pin, !(digitalRead(intLED1Pin)));  //Invert Current State of LED  
     delay(70);
     digitalWrite(intLED1Pin, !(digitalRead(intLED1Pin)));   
     delay(45);
     if (millis() > wifiLoopNow + 30000) {  // WiFi Stuck?  Reboot ESP and start over...
       debugLn(F("ESP: WiFi Failed. Restarting ESP."));
       delay(100);
       ESP.restart();      
     }
  }
  digitalWrite(intLED1Pin, LEDoff);
}
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// onWifiConnect                                                 //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  long rssi = WiFi.RSSI();
  debugLn(String(F("WIFI: Connected - IP: ")) + WiFi.localIP().toString() + " - RSSI: " + String(rssi) );
  connectToMqtt();
}
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// onWifiDisconnect                                              //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  debugLn(F("WIFI: Disconnected."));
  mqttReconnectTimer.detach(); // don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// connectToMqtt                                                 //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
void connectToMqtt() {
  debugLn(String(F("MQTT: Attempting connection to ")) + String(mqttHost) + " as " + mcuHostName);
  mqttClient.connect();
  mqttTryCount++;
  if (mqttTryCount > 15) {
    debugLn(F("ESP: MQTT Failed too many times. Restarting ESP."));
    delay(100);
    ESP.restart();      
  }  
}
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
bool checkBoundSensor(float newValue, float prevValue, float maxDiff) {
  return newValue < prevValue - maxDiff || newValue > prevValue + maxDiff;
}
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// Check Sensor                                                  //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
void checkSCT()
{
  debugLn(F("SCT: Read"));
  digitalWrite(intLED1Pin, LEDon);
  float sctNewCur = emon1.calcIrms(emonCalcIrms);
  digitalWrite(intLED1Pin, LEDoff);  
  dtostrf(sctNewCur, 2, 2, sSCTcurTemp);
  debugLn(String(F("SCT: Results - Current: "))+ sSCTcurTemp);
  // check curr difference - update the status?
  if (checkBoundSensor(sctNewCur, sctCur, sctDiff)) {
    sctCur = sctNewCur;
    dtostrf(sctCur, 2, 2, sSCTcur);
    debugLn(F("SCT: Publish new value via MQTT"));
    digitalWrite(intLED1Pin, LEDon);
    mqttClient.publish(sctTopic, mqttQOSLevel, false, String(sSCTcur).c_str());   
    digitalWrite(intLED1Pin, LEDoff);     
  }
}  

// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// onMqttConnect                                                 //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
void onMqttConnect(bool sessionPresent) {
  debugLn(F("MQTT: Connected"));
  mqttTryCount = 0;
  mqttClient.publish(lwtTopic, 2, true, mqttBirth);
  // Setup Sensor Polling
  sctTick.attach(sctPoll, checkSCT);
  rePushTick.attach(rePushPoll, rePushVals);
}

// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// onMqttDisconnect                                              //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  debugLn(F("MQTT: Disconnected."));
  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
  // if MQTT is disconnected stop polling sensors
  sctTick.detach();
  rePushTick.detach();
}

void flipLED1() {
  digitalWrite(intLED1Pin, !(digitalRead(intLED1Pin)));  //Invert Current State of LED  
}

const char *getDeviceID() {
  char *identifier = new char[30];
  os_strcpy(identifier, hostName);
  strcat_P(identifier, PSTR("-"));

  char cidBuf[7];
  sprintf(cidBuf, "%06X", ESP.getChipId());
  os_strcat(identifier, cidBuf);

  return identifier;
}

// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// ESP Setup                                                     //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
void setup() {
  #ifdef DEBUGSERIAL
    Serial.begin(115200);
    while(!Serial) {} // Wait
    Serial.println();
  #endif  
  debugLn(String(F("digiDryerMon - Build: ")) + F(__DATE__) + " " +  F(__TIME__));
  // build hostname with last 6 of MACID
  os_strcpy(mcuHostName, getDeviceID());

  // ~~~~ Set MQTT Topics
  sprintf_P(lwtTopic, PSTR("%s/LWT"), mcuHostName);
  sprintf_P(sctTopic, PSTR("%s/SCT"), mcuHostName);
  sprintf_P(rssiTopic, PSTR("%s/RSSI"), mcuHostName);
  sprintf_P(buildTopic, PSTR("%s/BUILD"), mcuHostName);
 
  // ~~~~ Set PIN Modes
  pinMode(intLED1Pin,OUTPUT); 
  pinMode(currentPin,INPUT);

  // ~~~~ Setup emonlib
  emon1.current(currentPin, emonCurrent);
  emon1.calcIrms(emonCalcIrms);

  delay(10);  

  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);

  // setup MQTT
  mqttClient.setWill(lwtTopic,2,true,mqttDeath,0);
  mqttClient.setCredentials(mqttUser,mqttPass);
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.setServer(mqttHost, mqttPort);
  mqttClient.setMaxTopicLength(512);
  mqttClient.setClientId(mcuHostName);

  connectToWifi();
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
  #ifdef DEBUGTELNET
    // Setup telnet server for remote debug output
    telnetServer.setNoDelay(true);
    telnetServer.begin();
    debugLn(String(F("Telnet: Started on port 23 - IP:")) + WiFi.localIP().toString());
  #endif

  // OTA Flash Sets
  ArduinoOTA.setPort(OTAport);
  ArduinoOTA.setHostname(mcuHostName);
  ArduinoOTA.setPassword((const char *)OTApassword);

  ArduinoOTA.onStart([]() {
    Serial.println("Starting");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  // Setup HTTP Flash Page
  httpUpdater.setup(&httpServer, "/flash", update_username, update_password);
  httpServer.on("/restart", []() {
    debugLn(F("HTTP: Restart request received."));
    httpServer.sendHeader("Access-Control-Allow-Origin", "*");
    httpServer.send(200, "text/plain", "Restart command sent to ESP Chip..." );
    delay(100);
    ESP.restart();
  });
  
  httpServer.begin();
  debugLn(F("ESP: Boot completed - Starting main loop"));
}
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// rePushVals                                                    //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
void rePushVals() {
    debugLn(F("ESP: RePushingVals to MQTT"));
    mqttClient.publish(sctTopic, mqttQOSLevel, false, String(sSCTcurTemp).c_str());      
    mqttClient.publish(rssiTopic, mqttQOSLevel, false, String(WiFi.RSSI()).c_str()); 
    mqttClient.publish(buildTopic, mqttQOSLevel, false, String(String(F("digiDryerMon - Build: ")) + F(__DATE__) + " " +  F(__TIME__) + " - " + WiFi.localIP().toString()).c_str()); 
}

// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// Telnet                                                        //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
#ifdef DEBUGTELNET
void handleTelnetClient()
{ 
  if (telnetServer.hasClient())
  {
    // client is connected
    if (!telnetClient || !telnetClient.connected())
    {
      if (telnetClient)
        telnetClient.stop();                   // client disconnected
      telnetClient = telnetServer.available(); // ready for new client
    }
    else
    {
      telnetServer.available().stop(); // have client, block new connections
    }
  }
  // Handle client input from telnet connection.
  if (telnetClient && telnetClient.connected() && telnetClient.available())
  {
    // client input processing
    while (telnetClient.available())
    {
      // Read data from telnet just to clear out the buffer
      telnetClient.read();
    }
  }
}
#endif

// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// Serial and Telnet Log Handler                                 //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
void debugLn(String debugText)
{ 
  String debugTimeText = "[+" + String(float(millis()) / 1000, 3) + "s] " + debugText;
  #ifdef DEBUGSERIAL
    Serial.println(debugTimeText);
    Serial.flush();
  #endif
  #ifdef DEBUGTELNET
    if (telnetClient.connected())
    {
      debugTimeText += "\r\n";
      const size_t len = debugTimeText.length();
      const char *buffer = debugTimeText.c_str();
      telnetClient.write(buffer, len);
      handleTelnetClient();
    }
  #endif
}
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
// ESP Loop                                                      //
// =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-= //
void loop() {
  ArduinoOTA.handle();
  httpServer.handleClient();
  #ifdef DEBUGTELNET
    handleTelnetClient();
  #endif
  if (initBoot) { // on first loop pull sensors
    delay(2000);  // hold up before first pull, had exception issues in mqtt connect on early pulls on some sensor types
    initBoot = false;
    if (WiFi.isConnected() && mqttClient.connected()) {
      checkSCT();
      delay(20);
      mqttClient.publish(rssiTopic, mqttQOSLevel, false, String(WiFi.RSSI()).c_str()); 
      mqttClient.publish(buildTopic, mqttQOSLevel, false, String(String(F("digiDryerMon - Build: ")) + F(__DATE__) + " " +  F(__TIME__) + " - " + WiFi.localIP().toString()).c_str()); 
      debugLn(String(F("MQTT: digiDryerMon - Build: ")) + F(__DATE__) + " " +  F(__TIME__) + " - " + WiFi.localIP().toString());
    } 
  }
} 
