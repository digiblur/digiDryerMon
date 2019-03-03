/************ WIFI and MQTT INFORMATION (CHANGE THESE FOR YOUR SETUP) ******************/
#define hostName "digiDryerMon"  

#define wifiSSID "Your SSID"
#define wifiPass "Your Password"

#define mqttHost "192.168.1.x" // mqtt IP/name
#define mqttPort 1883
#define mqttUser "mqtt username"
#define mqttPass "mqtt password"

#define mqttBirth "Online"
#define mqttDeath "Offline"

/******* HTTP /flash page ID/pass **********/
const char* update_username = "admin";
const char* update_password = "OTAdigi"; // change this to a password you want to use when you upload via HTTP

/******* Debug **************/
#define DEBUGSERIAL
#define DEBUGTELNET  // Open a read-only telnet debug port

/******* OTA **************/
int OTAport = 8266;
#define OTApassword "OTAdigi" // change/uncomment this to a password you want to use when you upload OTA
