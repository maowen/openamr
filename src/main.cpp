#include <Arduino.h>
#include <vector>
#include <ESP8266WiFi.h>
// #include <ESP8266HTTPClient.h>
#include <ESPAsyncTCP.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Esp.h>
#include <NtpClientLib.h>
// #include <NTPtimeESP.h>
#include <Ticker.h>
#include <Time.h>
#include <WiFiUdp.h>
#include <AsyncMqttClient.h>

// NTP Options:
// NtpClientLib - No ISO time. Supports DST

extern "C" {
  #include <user_interface.h>
  #include "../lib/ert-amr/amr.h"
}

#define MAX_POST_DATA_SIZE 256
// #define AMR_METER_ID 27367479
const int AMR_IDM_METER_ID         = 32839945;
const int AMR_SCM_METER_ID_CONSUMP = 32839945;
const int AMR_SCM_METER_ID_EXCESS  = 32839946;
const int AMR_SCM_METER_ID_RESIDUAL= 32839947;
const int AMR_SCM_WH_CONVERSION = 10; // 10 Wh to Wh

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

const char THINGSBOARD_DEVICE_ID[] = "f1d7e9b0-fa7b-11e7-abe9-1d8d2edf4f93";
const char THINGSBOARD_ACCESS_TOKEN[] = "Va1ce6shTIPj9soYVtms";
const char THINGSBOARD_LOCAL_DEVICE_ID[] = "e81d6670-33dd-11e8-81c0-a52d9fdb371f";
const char THINGSBOARD_LOCAL_ACCESS_TOKEN[] = "uoO0MCfSe1o5MD8q5qbT";
AsyncMqttClient mqttClient;
String mqttPayload;
String mqttTopic;
AsyncMqttClient mqttClientLocal;
String mqttPayloadLocal;
String mqttTopicLocal;
const String mqttTelemTopic = "v1/devices/me/telemetry";
char mqttBuffer[256];


const char* ssid = "Lugubrious";
const char* password = "iLoVeMyWiFe";
MDNSResponder mdns;

const char i7559IP[] = "192.168.1.13";
const char rpi2IP[] = "192.168.1.101";
const int msgServerPort = 3456;
WiFiUDP udp;

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
  
  /*wl_status_t status = WiFi.status(); // Arduino way*/
  
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

struct HttpRequest {

  enum HTTP_METHOD {
    HTTP_NONE=0,
    HTTP_GET,
    HTTP_POST
  };

  HTTP_METHOD method;
  String reqStr;
  std::vector<char> data;
  uint16_t reqId;
};

// static AsyncClient * httpClient = NULL;
static uint16_t httpReqCnt = 0;

bool httpRequest(HttpRequest::HTTP_METHOD method, const String & uri, const char * data, size_t dataLen, void * cb) {
  AsyncClient *httpClient = new AsyncClient();
  if (!httpClient)
  {
    Serial.printf("httpRequest: Failed to create AsyncClient\n");
    return false;
  }

  HttpRequest *httpReq = new HttpRequest();
  if (!httpReq)
  {
    Serial.printf("httpRequest: Failed to create HttpRequest\r\n");
    return false;
  }

  httpReq->method = method;
  httpReq->reqStr.reserve(256);

  if (method == HttpRequest::HTTP_GET)
  {
    // Serial.printf("httpRequest: Method = GET\r\n");
    httpReq->reqStr = "GET ";
  }
  else if (method == HttpRequest::HTTP_POST)
  {
    return false; // Not yet supported
    httpReq->reqStr = "POST ";
  }
  else
  {
    return false;
  }

  String path;
  String host;

  int pathStart = uri.indexOf('/');
  if (pathStart >= 0)
  {
    path = uri.substring(pathStart);
    host = uri.substring(0, pathStart);
  }
  else
  {
    path = "/";
    host = uri;
  }
  httpReq->reqStr += path + " HTTP/1.1\r\nHOST: ";
  httpReq->reqStr += host + "\r\n";

  httpReq->data.clear();
  if (data && dataLen > 0)
  {
    httpReq->data.insert(httpReq->data.end(), data, data + dataLen);
  }
  // Last line of request
  httpReq->reqStr += "\r\n";
  httpReq->reqId = ++httpReqCnt;

  httpClient->onError([](void *arg, AsyncClient *client, int error) {
    Serial.printf("Http Connect Error!\r\n");
    if (client)
    {
      delete client;
    }
    if (arg)
    {
      HttpRequest *req = (HttpRequest *)arg;
      delete req;
    }
  }, httpReq);

  httpClient->onConnect([](void *arg, AsyncClient *client) {
    if (!client)
    {
      Serial.printf("onConnect Error! No client provided\r\n");
      return;
    }

    HttpRequest *req = NULL;
    if (arg)
    {
      req = (HttpRequest *)arg;
    }

    // Serial.printf("Connected (Req: %u)\n", req ? req->reqId : 0);
    client->onError(NULL, NULL);

    client->onData([](void *arg, AsyncClient *client, void *data, size_t len) {
      // TODO Parse the response status code and content
      // Serial.printf("\r\nHTTP Response. len=%u\r\n", len);
      // uint8_t *d = (uint8_t *)data;
      // for (size_t i = 0; i < len; ++i)
      // {
      //   Serial.write(d[i]);
      // }
    }, req);

    client->onDisconnect([](void *arg, AsyncClient *client) {
      // Serial.printf("\r\nDisconnected\r\n\r\n");
      if (client) {
        delete client;
      }

      if (arg) {
        HttpRequest *req = (HttpRequest *)arg;
        delete req;
      }
    }, req);

    if (req)
    {
      // Serial.printf("\r\nSending HTTP Request:\r\n");
      // Serial.print(req->reqStr.c_str());
      // Serial.print("\r\n");
      client->write(req->reqStr.c_str());
      if (req->data.size() > 0)
      {
        // Serial.printf("Data: \r\n");
        // for (size_t i = 0; i < req->data.size(); ++i)
        // {
        //   Serial.print(req->data[i]);
        // }
        client->write(req->data.data(), req->data.size());
      }
    }
  }, httpReq);

  if (!httpClient->connect(host.c_str(), 80)) {
    Serial.printf("httpRequest: Connect Fail\r\n");
    delete httpClient;
    httpClient = NULL;
  }
}

bool getRequest(String & uri, void * cb) {
  return httpRequest(HttpRequest::HTTP_GET, uri, NULL, 0, cb);
}

bool postRequest(String & uri, const char * body, uint32_t bodyLen, void * cb) {
  return httpRequest(HttpRequest::HTTP_POST, uri, body, bodyLen, cb);
}

static int publishMqttPayload(AsyncMqttClient &client, const String &topic, const String &payload) {
  int success = 0;
  if (client.connected()) {
    const uint8_t qos = 0;
    const uint8_t retain = 1;
    success = client.publish(topic.c_str(), qos, retain, payload.c_str());
  }

  if (success) {
    printf("%s: Topic: %s Payload: %s\n\r", __FUNCTION__, topic.c_str(), payload.c_str());
  }
  else {
    Serial.printf("%s: Failed to Publish MQTT message: MQTT Client Not Connected!\r\n");
  }
  return success;
}
void publishOnMqttConnect(bool sessionPresent) {
  if (mqttPayload != "") {
    int success = publishMqttPayload(mqttClient, mqttTopic, mqttPayload);
    if (success) {
      mqttPayload = "";
    }
  }
}

void publishOnMqttConnectLocal(bool sessionPresent) {
  if (mqttPayloadLocal != "") {
    int success = publishMqttPayload(mqttClientLocal, mqttTopicLocal, mqttPayloadLocal);
    if (success) {
      mqttPayloadLocal = "";
    }
  }
}


int thingsboardPublishTelem(const String &payload) {
  int success = 0;
  if (mqttClient.connected()) {
    success = publishMqttPayload(mqttClient, mqttTelemTopic, payload);
  }

  if (!success) {
      mqttTopic = mqttTelemTopic;
      mqttPayload = payload;
      mqttClient.connect();
  }

  success = 0;
  if (mqttClientLocal.connected()) {
    success = publishMqttPayload(mqttClientLocal, mqttTelemTopic, payload);
  }

  if (!success) {
      mqttTopicLocal = mqttTelemTopic;
      mqttPayloadLocal = payload;
      mqttClientLocal.connect();
  }

  return 0;
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

    time_t t = now(); //NTP.getTime();;
    unsigned long t_ms = millis();
    // time_t t = NTP.getTime();
    printf("getTime=%lu millis=%lu\n\r", t, t_ms);

    if (uptime % 300 == 0)
    {
      String dateStr = "NONE"; // NTP.getTimeDateString();
      uint32_t freeHeap = ESP.getFreeHeap();

      Serial.printf("Uploading status log. %s uptime=%u free=%u\r\n",
                    dateStr.c_str(), uptime, freeHeap);
      // {"ts":1451649600512, "values":{"key1":"value1", "key2":"value2"}}
/*
      String espStatus = "api.thingspeak.com/update?key=" ESP_LOG_THINGSPEAK_API_KEY
                         "&timezone=America%2FDenver" +
                         String("&field1=") + String(chipId) +
                         "&field2=" + String(uptime) +
                         "&field3=" + String(freeHeap) +
                         "&field4=" + connectStatusStr() +
                         "&field5=" + String(logMsg);
      // getRequest(espStatus, NULL);
                         */
      String logPayload = String("{\"Uptime\":" + String(uptime) +
                         ", \"FreeHeap\":" + String(freeHeap) +
                         ", \"ConnectStatus\":\"" + connectStatusStr()) +
                        //  ", \"ConnectState\":\"" + String(wifi_station_get_connect_status()) +
                         "\"}";
      thingsboardPublishTelem(logPayload);
      os_printf("Logging ESP Status %s\n", logPayload.c_str());
      logMsg[0] = '\0';
    }

    uptime+=5;
}

void handleScmMsg(const AmrScmMsg * msg) {
  /*
    DateTime now = SystemClock.now(eTZ_UTC);
    String dateStr = now.toISO8601();
    printScmMsg(dateStr.c_str(), msg);
    */
    String dateStr = "NONE"; // NTP.getTimeDateString();

    // TODO Compute differential production and net usage values
    // TODO Compute running total for each day

    if (msg) {
      String scmPayload;
      if (msg->id == AMR_SCM_METER_ID_CONSUMP) {
        scmPayload = "{\"Consumption\":" + String(msg->consumption * AMR_SCM_WH_CONVERSION) + "}";
        thingsboardPublishTelem(scmPayload);
        printf("mqttMsg: %s\r\n", scmPayload.c_str());
        printScmMsg(dateStr.c_str(), msg);
      }
      else if (msg->id == AMR_SCM_METER_ID_EXCESS) {
        scmPayload = "{\"Production\":" + String(msg->consumption * AMR_SCM_WH_CONVERSION) + "}";
        thingsboardPublishTelem(scmPayload);
        printf("mqttMsg: %s\r\n", scmPayload.c_str());
        printScmMsg(dateStr.c_str(), msg);
      }
      else if (msg->id == AMR_SCM_METER_ID_RESIDUAL) {
        scmPayload = "{\"NetUsage\":" + String(msg->consumption * AMR_SCM_WH_CONVERSION) + "}";
        thingsboardPublishTelem(scmPayload);
        printf("mqttMsg: %s\r\n", scmPayload.c_str());
        printScmMsg(dateStr.c_str(), msg);
      }
      // Serial.printf("\r\n\r\nUploading SCM Msg (%s)\r\n", dateStr.c_str());
      // getRequest(
      //     "api.thingspeak.com/update?key=" AMR_THINGSPEAK_API_KEY
      //     /*"&created_at=" + dateStr +
      //         "&timezone=America%2FDenver" +*/
      //     "&field1=" +
      //         String(msg->consumption),
      //     NULL);
/*
      snprintf(mqttBuffer, sizeof(mqttBuffer),
               "{\"ertId\":%u, \"consumptionHR\":%u, \"diffConsumpHr\":%u, \"consumption\":%u, \"excess\":%u, "
               "\"residual\":%u, \"msgCnt\":%u, \"txTimeOffset\":%u}",
               msg->ertId,
               msg->data.x18.lastConsumptionHighRes, msg->data.x18.differentialConsumption[0],
               msg->data.x18.lastConsumption, msg->data.x18.lastExcess, msg->data.x18.lastResidual,
               msg->consumptionIntervalCount, msg->txTimeOffset);
               */
    }

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
    String dateStr = "NONE"; // NTP.getTimeDateString();

    // printScmPlusMsg(dateStr.c_str(), msg);

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
    /*if (msg->ertId != AMR_METER_ID) {
      return;
    }*/

    //time_t now = NTP.getTime();
    String dateStr = "NONE"; // NTP.getTimeDateString();
    if (msg && (msg->ertId == AMR_IDM_METER_ID)) {
      // Serial.printf("\r\n\r\nUploading IDM Msg (%s)\r\n", dateStr.c_str());
      /*thingSpeak.downloadString(*/
      // getRequest(
      //     "api.thingspeak.com/update?key=" AMR_THINGSPEAK_API_KEY
      //     /*"&created_at=" + dateStr +
      //             "&timezone=America%2FDenver" +*/
      //     "&field2=" +
      //         String(msg->data.x18.lastConsumptionHighRes) +
      //     "&field3=" +
      //         String(msg->data.x18.differentialConsumption[0]),
      //     NULL);
        
      // Mqtt data format: {"key1":"value1", "key2":"value2"} or
      // {"ts":1451649600512, "values":{"key1":"value1", "key2":"value2"}} for client side timestamp. Unix time with millisecond precision

      // TODO Use NTP timestamp when uploading data
      // TODO Shift NTP timestamp by TxTimeOffset 
      // TODO Use msgCnt and previous message count to upload telem for missed msgs

      String idmPayload =
        "{\"ConsumptionHR\":" + String(msg->data.x18.lastConsumptionHighRes) +
        ", \"DiffConsumptionHR\":" + String(msg->data.x18.differentialConsumption[0]) +
        ", \"ConsumptionLR\":" + String(msg->data.x18.lastConsumption * AMR_SCM_WH_CONVERSION) +
        ", \"ProductionLR\":" + String(msg->data.x18.lastExcess * AMR_SCM_WH_CONVERSION) +
        ", \"NetUsageLR\":" + String(msg->data.x18.lastResidual *  AMR_SCM_WH_CONVERSION) +
        ", \"IDMMsgCnt\":" + String(msg->consumptionIntervalCount) +
        ", \"TxTimeOffset\":" + String(msg->txTimeOffset) +
        "}";

/*
      snprintf(mqttBuffer, sizeof(mqttBuffer),
               "{\"ertId\":%u, \"consumptionHR\":%u, \"diffConsumpHr\":%u, \"consumption\":%u, \"excess\":%u, "
               "\"residual\":%u, \"msgCnt\":%u, \"txTimeOffset\":%u}",
               msg->ertId,
               msg->data.x18.lastConsumptionHighRes, msg->data.x18.differentialConsumption[0],
               msg->data.x18.lastConsumption, msg->data.x18.lastExcess, msg->data.x18.lastResidual,
               msg->consumptionIntervalCount, msg->txTimeOffset);
               */
      thingsboardPublishTelem(idmPayload);
      printf("mqttMsg: %s\r\n", idmPayload.c_str());

      printIdmMsg(dateStr.c_str(), msg);
      // return;
    }

    if (msg && msg->ertType == 0x18) {
               /*
      String mqttMsg = "{\"consumptionHR\":" + msg->data.x18.lastConsumptionHighRes;
      mqttMsg += ", \"diffConsumpHR\":";
      mqttMsg += msg->data.x18.differentialConsumption[0];
      mqttMsg += ", \"consumption\":";
      mqttMsg += msg->data.x18.lastConsumption;
      mqttMsg += ", \"excess\":";
      mqttMsg += msg->data.x18.lastExcess;
      mqttMsg += ", \"residual\":";
      mqttMsg += msg->data.x18.lastResidual;
      mqttMsg += ", \"msgCnt\":";
      mqttMsg += msg->consumptionIntervalCount;
      mqttMsg += ", \"txTimeOffset\":";
      mqttMsg += msg->txTimeOffset;
      mqttMsg += "}";

      printf("mqttMsg: %s\r\n", mqttMsg.c_str());
      */
    }

    if (msg) {
      // printIdmMsg(dateStr.c_str(), msg);
    }


    // thingSpeak.setPostBody("{}");
    // thingSpeak.setRequestContentType("application/json");
    // thingSpeak.downloadString("http://api.thingspeak.com/update", onDataReceived);
}

void handleAmrMsg(const void * msg, AMR_MSG_TYPE msgType, const uint8_t * data) {
  switch (msgType) {
    case AMR_MSG_TYPE_SCM:
      handleScmMsg((const AmrScmMsg *)msg);
      udp.beginPacket(rpi2IP, msgServerPort);
      udp.write(data, AMR_MSG_SCM_RAW_SIZE);
      udp.endPacket();
      udp.beginPacket(i7559IP, msgServerPort);
      udp.write(data, AMR_MSG_SCM_RAW_SIZE);
      udp.endPacket();
    break;
    case AMR_MSG_TYPE_SCM_PLUS:
      handleScmPlusMsg((const AmrScmPlusMsg *)msg);
    break;
    case AMR_MSG_TYPE_IDM:
      handleIdmMsg((const AmrIdmMsg *)msg);
      udp.beginPacket(rpi2IP, msgServerPort);
      udp.write(data, AMR_MSG_IDM_RAW_SIZE);
      udp.endPacket();
      udp.beginPacket(i7559IP, msgServerPort);
      udp.write(data, AMR_MSG_IDM_RAW_SIZE);
      udp.endPacket();
    break;
    default:
    break;
  }
}

WiFiEventHandler staGotIPHandler;
WiFiEventHandler staDisconnectHandler;

bool wifiFirstConnected = false;

void onSTAGotIP(WiFiEventStationModeGotIP ipInfo) {
  Serial.printf("Got IP: %s\r\n", ipInfo.ip.toString().c_str());
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  wifiFirstConnected = true;
  // NTP.begin();
  // NTP.begin("pool.ntp.org", 1, true, 0);
}

void onSTADisconnected(WiFiEventStationModeDisconnected eventInfo) {
  Serial.printf("Disconnected from SSID: %s\r\n", eventInfo.ssid.c_str());
  Serial.printf("Reason: %d\r\n", eventInfo.reason);
}

void setup(void){

  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  staGotIPHandler = WiFi.onStationModeGotIP(&onSTAGotIP);
  staDisconnectHandler = WiFi.onStationModeDisconnected(&onSTADisconnected);

  mqttClient.onConnect(publishOnMqttConnect);
  mqttClient.setCredentials(THINGSBOARD_ACCESS_TOKEN);
  mqttClient.setServer("demo.thingsboard.io", 1883);
  mqttClient.setClientId(THINGSBOARD_DEVICE_ID);

  mqttClientLocal.onConnect(publishOnMqttConnectLocal);
  mqttClientLocal.setCredentials(THINGSBOARD_LOCAL_ACCESS_TOKEN);
  mqttClientLocal.setServer(rpi2IP, 1883);
  mqttClientLocal.setClientId(THINGSBOARD_LOCAL_DEVICE_ID);

  // Enable WiFi
  WiFi.begin(ssid, password);
  // Disable WiFi
  // WiFi.disconnect();
  // WiFi.mode(WIFI_OFF);
  // WiFi.forceSleepBegin();

  Serial.println("");
  chipId = ESP.getChipId();
  Serial.printf("ESP8266 Chip ID: %u (0x%x)\r\n", chipId, chipId);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  staGotIPHandler = WiFi.onStationModeGotIP(&onSTAGotIP);
  staDisconnectHandler = WiFi.onStationModeDisconnected(&onSTADisconnected);

  if (mdns.begin("esp8266", WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }

  server.on("/", handleRoot);

  server.on("/inline", [](){
    server.send(200, "text/plain", "this works as well");
  });

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

  amrInit();
  registerAmrMsgCallback(&handleAmrMsg);
  amrEnable(true);

  logESPStats();
  amrProcessing.attach(1.0, amrProcessMsgs);
  logProcessing.attach(5.0, logESPStats);
}

void loop(void){
  server.handleClient();

  if (wifiFirstConnected) {
    NTP.begin("pool.ntp.org", 1, true, 0);
    wifiFirstConnected = false;
  }

  
}
