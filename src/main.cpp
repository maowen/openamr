#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Ticker.h>

extern "C" {
  #include <user_interface.h>
  #include "../lib/ert-amr/amr.h"
}
#define MAX_POST_DATA_SIZE 256
#define AMR_METER_ID 27367479
#define AMR_THINGSPEAK_API_KEY "HVRMOK9KTZ1JUZJO"
#define ESP_LOG_THINGSPEAK_API_KEY "Y0XGGA3S6DFY1QOQ"
#define AMR_THINGSPEAK_UPDATE_URL "http://184.106.153.149/update"
static uint32_t chipId = 0;
static uint32_t uptime = 0;
static uint32_t disconnectCnt = 0;
static struct rst_info* rtc_info = NULL;
static char logMsg[128] = "";
Ticker amrProcessing;
Ticker logProcessing;


const char* ssid = "Lugubrious";
const char* password = "iLoVeMyWiFe";
MDNSResponder mdns;

ESP8266WebServer server(80);

const int led = 13;

void handleRoot() {
  digitalWrite(led, 1);
  server.send(200, "text/plain", "hello from esp8266!");
  digitalWrite(led, 0);
}

void handleNotFound(){
  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(led, 0);
}

const char * connectStatusStr() {
    uint8_t status = wifi_station_get_connect_status();
    switch (status) {
        case STATION_IDLE: return "STATION_IDLE";
        case STATION_CONNECTING: return "STATION_CONNECTION";
        case STATION_WRONG_PASSWORD: return "STATION_WRONG_PASSWORD";
        case STATION_NO_AP_FOUND: return "STATION_NO_AP_FOUND";
        case STATION_CONNECT_FAIL: return "STATION_CONNECT_FAIL";
        case STATION_GOT_IP: return "STATION_GOT_IP";
    }
}

/*
int onDataSent(HttpConnection& client, bool successful)
{
	if (successful)
		Serial.println("Success sent");
	else
		Serial.println("Failed");

	String response = client.getResponseString();
	Serial.println("Server response: '" + response + "'");
	if (response.length() > 0)
	{
		int intVal = response.toInt();

		if (intVal == 0)
			Serial.println("Sensor value wasn't accepted. May be we need to wait a little?");
	}

	return 0;
}
*/

void logESPStats() {
    /*
     * 1 - ID
     * 2 - Uptime
     * 3 - Heap
     * 4 - Wifi Status
     * 6 - Msg
     */

    if (uptime % 300 == 0) {
        uint32_t freeHeap = system_get_free_heap_size();

        String espStatus = "http://api.thingspeak.com/update?key="
                ESP_LOG_THINGSPEAK_API_KEY
                "&timezone=America%2FDenver" +
                String("&field1=") + String(chipId) +
                "&field2=" + String(uptime) +
                "&field3=" + String(freeHeap) +
                "&field4=" + connectStatusStr() +
                "&field5=" + String(logMsg);
        os_printf("Logging ESP Status %s\n", espStatus.c_str());
        /*
        thingSpeak.downloadString( espStatus, onDataSent);
        logMsg[0] = '\0';
        */
    }

    uptime+=5;
}

void handleScmMsg(const AmrScmMsg * msg) {
  /*
    DateTime now = SystemClock.now(eTZ_UTC);
    String dateStr = now.toISO8601();
    printScmMsg(dateStr.c_str(), msg);
    */
    printScmMsg("DATETIME", msg);


    /*
    char postData[MAX_POST_DATA_SIZE] = {0};
    if (scmMsg.id == AMR_METER_ID) {
        os_sprintf(postData,
                "api_key=%s&field1=%u",
                AMR_THINGSPEAK_API_KEY,
                scmMsg.consumption);

           http_post(AMR_THINGSPEAK_UPDATE_URL,
           postData,
           "",
           scm_post_callback);
    }
    */
}

void handleScmPlusMsg(const AmrScmPlusMsg * msg) {
  /*
    DateTime now = SystemClock.now();
    String dateStr = now.toISO8601();
    printScmPlusMsg(dateStr.c_str(), msg);
    */
    printScmPlusMsg("DATETIME", msg);

    /*
    char postData[MAX_POST_DATA_SIZE] = {0};
    if (scmMsg.id == AMR_METER_ID) {
        os_sprintf(postData,
                "api_key=%s&field1=%u",
                AMR_THINGSPEAK_API_KEY,
                scmMsg.consumption);

           http_post(AMR_THINGSPEAK_UPDATE_URL,
           postData,
           "",
           scm_post_callback);
    }
    */
}

void handleIdmMsg(const AmrIdmMsg * msg) {
  /*
    DateTime now = SystemClock.now(eTZ_UTC);
    time_t epoch = now.toUnixTime();
    
    String dateStr = now.toISO8601();
    Serial.printf("Epoch: %lu\t", epoch);
    printIdmMsg(dateStr.c_str(), msg);
    */
    printIdmMsg("DATETIME", msg);

    if (msg && msg->ertId == AMR_METER_ID) {

        Serial.print("Uploading IDM Msg\n");
        /*
        thingSpeak.downloadString(
                "http://api.thingspeak.com/update?key="
                AMR_THINGSPEAK_API_KEY
                "&created_at=" + dateStr +
                "&timezone=America%2FDenver" +
                "&field2=" + String(msg->lastConsumption) +
                "&field3=" + String(msg->differentialConsumption[0]),
                onDataSent);
                */

        // thingSpeak.setPostBody("{}");
        // thingSpeak.setRequestContentType("application/json");
        // thingSpeak.downloadString("http://api.thingspeak.com/update", onDataReceived);
    }
}

void setup(void){
  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.println("");
  chipId = system_get_chip_id();
  Serial.printf("ESP8266 Chip ID: %u (0x%x)\n", chipId, chipId);
  printf("Test Printf\n");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (mdns.begin("esp8266", WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }

  printf("Test Printf\n");

  server.on("/", handleRoot);

  server.on("/inline", [](){
    server.send(200, "text/plain", "this works as well");
  });

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

  amrInit();
  registerIdmMsgCallback(&handleIdmMsg);
  registerScmMsgCallback(&handleScmMsg);
  registerScmPlusMsgCallback(&handleScmPlusMsg);
  amrEnable(true);

  amrProcessing.attach(1.0, amrProcessMsgs);
  logProcessing.attach(5.0, logESPStats);

}

void loop(void){
  server.handleClient();
}