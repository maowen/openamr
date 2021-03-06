#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <vector>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Esp.h>
#include "static/index.html.gz.h"
#include <AsyncMqttClient.h>
#include <Embedis.h>
#include <FS.h>
#include <Ticker.h>
#include <sntp.h>
Embedis embedis(Serial);

#include <EEPROM.h>
#include "spi_flash.h"

extern "C" {
#include <user_interface.h>
#include "../lib/ert-amr/amr.h"
}

#include "ErtAmrMqttClient.hpp"

// #define AMR_METER_ID 27367479
const int AMR_IDM_METER_ID = 32839945;
const int AMR_SCM_METER_ID_CONSUMP = 32839945;
const int AMR_SCM_METER_ID_EXCESS = 32839946;
const int AMR_SCM_METER_ID_RESIDUAL = 32839947;
const int AMR_SCM_WH_CONVERSION = 10;  // 10 Wh to Wh

const uint32_t EPOCH_TIME_MIN_SECONDS = 1575158400;

static uint32_t chipId = 0;
// static struct rst_info* rtc_info = NULL;
// static char logMsg[128] = "";
Ticker amrProcessing;
Ticker logProcessing;

// Active Thingsboard ID
// const char THINGSBOARD_DEVICE_ID[] = "f1d7e9b0-fa7b-11e7-abe9-1d8d2edf4f93";
// const char THINGSBOARD_ACCESS_TOKEN[] = "Va1ce6shTIPj9soYVtms";

// Thingsboard Test Device
const char THINGSBOARD_TEST_DEVICE_ID[] =
    "e026fda0-cecd-11e9-862b-9deda36b0cc7";
const char THINGSBOARD_TEST_ACCESS_TOKEN[] = "an3rLk4odSos4Q3o3SOF";

const char *ssid = "Lugubrious";
const char *password = "iLoVeMyWiFe";
MDNSResponder mdns;

AsyncWebServer *server;
char last_modified[50];

const int led = 13;

ertamr::ErtAmrMqttClient mqttInflux;
ertamr::ReportToInfluxdbLine influxSerializer;
ertamr::ErtAmrMqttClient mqttThingsboard;
ertamr::ReportToJson jsonSerializer;

char deviceId[16];

/*
String getContentType(String filename) { // convert the file extension to the
MIME type if (filename.endsWith(".html")) return "text/html"; else if
(filename.endsWith(".css")) return "text/css"; else if
(filename.endsWith(".js")) return "application/javascript"; else if
(filename.endsWith(".ico")) return "image/x-icon"; return "text/plain";
}

bool handleFileRead(String path) { // send the right file to the client (if it
exists) Serial.println("handleFileRead: " + path); if (path.endsWith("/")) path
+= "index.html";         // If a folder is requested, send the index file String
contentType = getContentType(path);            // Get the MIME type if
(SPIFFS.exists(path)) {                            // If the file exists File
file = SPIFFS.open(path, "r");                 // Open it size_t sent =
server.streamFile(file, contentType); // And send it to the client file.close();
// Then close the file again return true;
  }
  Serial.println("\tFile Not Found");
  return false;                                         // If the file doesn't
exist, return false
}

void handleRoot() {
  digitalWrite(led, 1);
  if (SPIFFS.exists("index.html")) {
    File f = SPIFFS.open("index.html", "r");
    server.streamFile(f, "text/html");
    // server.send(200, "text/plain", "hello from esp8266!");
  }
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
*/

void onHome(AsyncWebServerRequest *request) {
  if (request->header("If-Modified-Since").equals(last_modified)) {
    request->send(304);
  } else {
    printf("%s: serving home\n\r", __FUNCTION__);
    AsyncWebServerResponse *response = request->beginResponse_P(
        200, "text/html", index_html_gz, index_html_gz_len);
    response->addHeader("Content-Encoding", "gzip");
    response->addHeader("Last-Modified", last_modified);
    request->send(response);
  }
}

const char *connectStatusStr() {
  /*wl_status_t status = WiFi.status(); // Arduino way*/

  uint8_t status = wifi_station_get_connect_status();
  switch (status) {
    case STATION_IDLE:
      return "STATION_IDLE";
    case STATION_CONNECTING:
      return "STATION_CONNECTION";
    case STATION_WRONG_PASSWORD:
      return "STATION_WRONG_PASSWORD";
    case STATION_NO_AP_FOUND:
      return "STATION_NO_AP_FOUND";
    case STATION_CONNECT_FAIL:
      return "STATION_CONNECT_FAIL";
    case STATION_GOT_IP:
      return "STATION_GOT_IP";
    default:
      return "UNKNOWN";
  }
}

struct HttpRequest {
  enum HTTP_METHOD { HTTP_NONE = 0, HTTP_GET, HTTP_POST };

  HTTP_METHOD method;
  String reqStr;
  std::vector<char> data;
  uint16_t reqId;
};

// static AsyncClient * httpClient = NULL;
static uint16_t httpReqCnt = 0;

bool httpRequest(HttpRequest::HTTP_METHOD method, const String &uri,
                 const char *data, size_t dataLen, void *cb) {
  AsyncClient *httpClient = new AsyncClient();
  if (!httpClient) {
    Serial.printf("httpRequest: Failed to create AsyncClient\n");
    return false;
  }

  HttpRequest *httpReq = new HttpRequest();
  if (!httpReq) {
    Serial.printf("httpRequest: Failed to create HttpRequest\r\n");
    return false;
  }

  httpReq->method = method;
  httpReq->reqStr.reserve(256);

  if (method == HttpRequest::HTTP_GET) {
    // Serial.printf("httpRequest: Method = GET\r\n");
    httpReq->reqStr = "GET ";
  } else if (method == HttpRequest::HTTP_POST) {
    return false;  // Not yet supported
    httpReq->reqStr = "POST ";
  } else {
    return false;
  }

  String path;
  String host;

  int pathStart = uri.indexOf('/');
  if (pathStart >= 0) {
    path = uri.substring(pathStart);
    host = uri.substring(0, pathStart);
  } else {
    path = "/";
    host = uri;
  }
  httpReq->reqStr += path + " HTTP/1.1\r\nHOST: ";
  httpReq->reqStr += host + "\r\n";

  httpReq->data.clear();
  if (data && dataLen > 0) {
    httpReq->data.insert(httpReq->data.end(), data, data + dataLen);
  }
  // Last line of request
  httpReq->reqStr += "\r\n";
  httpReq->reqId = ++httpReqCnt;

  httpClient->onError(
      [](void *arg, AsyncClient *client, int error) {
        Serial.printf("Http Connect Error!\r\n");
        if (client) {
          delete client;
        }
        if (arg) {
          HttpRequest *req = (HttpRequest *)arg;
          delete req;
        }
      },
      httpReq);

  httpClient->onConnect(
      [](void *arg, AsyncClient *client) {
        if (!client) {
          Serial.printf("onConnect Error! No client provided\r\n");
          return;
        }

        HttpRequest *req = NULL;
        if (arg) {
          req = (HttpRequest *)arg;
        }

        // Serial.printf("Connected (Req: %u)\n", req ? req->reqId : 0);
        client->onError(NULL, NULL);

        client->onData(
            [](void *arg, AsyncClient *client, void *data, size_t len) {
              // TODO Parse the response status code and content
              // Serial.printf("\r\nHTTP Response. len=%u\r\n", len);
              // uint8_t *d = (uint8_t *)data;
              // for (size_t i = 0; i < len; ++i)
              // {
              //   Serial.write(d[i]);
              // }
            },
            req);

        client->onDisconnect(
            [](void *arg, AsyncClient *client) {
              // Serial.printf("\r\nDisconnected\r\n\r\n");
              if (client) {
                delete client;
              }

              if (arg) {
                HttpRequest *req = (HttpRequest *)arg;
                delete req;
              }
            },
            req);

        if (req) {
          // Serial.printf("\r\nSending HTTP Request:\r\n");
          // Serial.print(req->reqStr.c_str());
          // Serial.print("\r\n");
          client->write(req->reqStr.c_str());
          if (req->data.size() > 0) {
            // Serial.printf("Data: \r\n");
            // for (size_t i = 0; i < req->data.size(); ++i)
            // {
            //   Serial.print(req->data[i]);
            // }
            client->write(req->data.data(), req->data.size());
          }
        }
      },
      httpReq);

  if (!httpClient->connect(host.c_str(), 80)) {
    Serial.printf("httpRequest: Connect Fail\r\n");
    delete httpClient;
    httpClient = NULL;
  }

  return true;
}

uint32_t get_current_timestamp() {
  uint32_t tstamp_s = sntp_get_current_timestamp();
  return tstamp_s > EPOCH_TIME_MIN_SECONDS ? tstamp_s : 0;
}

uint32_t nextLogTime_s = 5;

void logESPStats() {
  uint32_t tstamp_s = get_current_timestamp();
  uint32_t uptime_us = system_get_time();
  uint32_t uptime_s = uptime_us / 1000000;
  Serial.printf("tstamp_s=%u uptime=%u\n\r", tstamp_s, uptime_s);

  if (uptime_s > nextLogTime_s) {
    uint32_t freeHeap = ESP.getFreeHeap();

    ertamr::LogReport logReport;
    logReport.tstamp_s = tstamp_s;
    logReport.deviceId = deviceId;
    logReport.freeHeap = freeHeap;
    logReport.uptime_s = uptime_s = uptime_s;
    logReport.connectStatus = connectStatusStr();

    Serial.printf("Log stats: tstamp_s=%u uptime_s=%u free=%u\r\n", tstamp_s,
                  uptime_s, freeHeap);

    mqttInflux.publishLogReport(logReport);
    mqttThingsboard.publishLogReport(logReport);

    nextLogTime_s = uptime_s + 300;
  }
}

void handleScmMsg(const AmrScmMsg *msg) {
  // TODO Compute differential production and net usage values
  // TODO Compute running total for each day

  if (msg) {
    bool publishMsg = false;

    ertamr::ScmReport report;
    report.tstamp_s = get_current_timestamp();
    report.wattHrs = msg->consumption * AMR_SCM_WH_CONVERSION;
    report.deviceId = deviceId;

    if (msg->id == AMR_SCM_METER_ID_CONSUMP) {
      report.ertId = AMR_SCM_METER_ID_CONSUMP;
      report.scmType = ertamr::SCM_TYPE_CONSUMPTION;
      publishMsg = true;
    } else if (msg->id == AMR_SCM_METER_ID_EXCESS) {
      report.ertId = AMR_SCM_METER_ID_CONSUMP;
      report.scmType = ertamr::SCM_TYPE_PRODUCTION;
      publishMsg = true;
    } else if (msg->id == AMR_SCM_METER_ID_RESIDUAL) {
      report.ertId = AMR_SCM_METER_ID_CONSUMP;
      report.scmType = ertamr::SCM_TYPE_NET_USAGE;
      publishMsg = true;
    }

    if (publishMsg) {
      printScmMsg(get_current_timestamp(), msg);
      mqttInflux.publishScmReport(report);
      mqttThingsboard.publishScmReport(report);
    }
  }
}

void handleScmPlusMsg(const AmrScmPlusMsg *msg) {
  // printScmPlusMsg(get_current_timestamp(), msg);
}

void handleIdmMsg(const AmrIdmMsg *msg) {
  if (msg && (msg->ertId == AMR_IDM_METER_ID)) {
    // TODO Use msgCnt and previous message count to upload telem for missed
    // msgs
    ertamr::IdmReport report;
    report.tstamp_s = get_current_timestamp();
    report.ertId = msg->ertId;
    report.deviceId = deviceId;
    report.consumption_Wh = msg->data.x18.lastConsumptionHighRes;
    report.diffConsumption_Wh = msg->data.x18.differentialConsumption[0];
    report.consumptionLR_Wh =
        msg->data.x18.lastConsumption * AMR_SCM_WH_CONVERSION;
    report.productionLR_Wh = msg->data.x18.lastExcess * AMR_SCM_WH_CONVERSION;
    report.netUsageLR_Wh = msg->data.x18.lastResidual * AMR_SCM_WH_CONVERSION;
    report.msgCnt = msg->consumptionIntervalCount;
    report.txTimeOffset_ms = msg->txTimeOffset;

    printIdmMsg(get_current_timestamp(), msg);
    mqttInflux.publishIdmReport(report);
    mqttThingsboard.publishIdmReport(report);
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
    // printIdmMsg(get_current_timestamp(), msg);
  }
}

void handleAmrMsg(const void *msg, AMR_MSG_TYPE msgType, const uint8_t *data) {
  switch (msgType) {
    case AMR_MSG_TYPE_SCM:
      handleScmMsg((const AmrScmMsg *)msg);
      break;
    case AMR_MSG_TYPE_SCM_PLUS:
      handleScmPlusMsg((const AmrScmPlusMsg *)msg);
      break;
    case AMR_MSG_TYPE_IDM:
      handleIdmMsg((const AmrIdmMsg *)msg);
      break;
    default:
      break;
  }
}

void setup_EEPROM(const String &dict) {
  EEPROM.begin(SPI_FLASH_SEC_SIZE);
  Embedis::dictionary(dict, SPI_FLASH_SEC_SIZE,
                      [](size_t pos) -> char { return EEPROM.read(pos); },
                      [](size_t pos, char value) { EEPROM.write(pos, value); },
                      []() { EEPROM.commit(); });
  // LOG( String() + F("[ Embedis : EEPROM dictionary installed ]") );
  // LOG( String() + F("[ Embedis : EEPROM dictionary selected ]") );
  // LOG( String() + F("[ Embedis : Note Bene! EEPROM 'Set' takes a long time on
  // Arduino101... (wait for it!)]") );
}

void setup_EEPROM() { setup_EEPROM(F("EEPROM")); }

WiFiEventHandler staGotIPHandler;
WiFiEventHandler staDisconnectHandler;

bool wifiFirstConnected = false;

void onSTAGotIP(WiFiEventStationModeGotIP ipInfo) {
  // Serial.printf("Got IP: %s\r\n", ipInfo.ip.toString().c_str());
  // Serial.println("");
  Serial.print("Connected to WiFi SSID: ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  wifiFirstConnected = true;

  mqttInflux.connect();
  mqttThingsboard.connect();
}

void onSTADisconnected(WiFiEventStationModeDisconnected eventInfo) {
  Serial.printf("Disconnected from SSID: %s\r\n", eventInfo.ssid.c_str());
  Serial.printf("Reason: %d\r\n", eventInfo.reason);
}

void setup(void) {
  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  Serial.begin(115200);
  // Wait for serial comm to be ready
  while (!Serial) {
    ;
  }

  // Cache the Last-Modifier header value
  snprintf_P(last_modified, sizeof(last_modified), PSTR("%s %s GMT"), __DATE__,
             __TIME__);

  embedis.reset();
  setup_EEPROM();

  snprintf(deviceId, sizeof(deviceId), "esp8266%06x", ESP.getChipId());

  String key_test;
  if (!embedis.get("keytest", key_test)) {
    printf("Embedis get: 'keytest'=%s\n\r", key_test.c_str());
    key_test = "Hello World!";
    printf("Embedis 'key_test' not found. Setting to %s\r\n", key_test.c_str());
    embedis.set("keytest", key_test);
    embedis.set("keytest", key_test);
    embedis.set("keytest", key_test);
  } else {
    printf("Embedis get: 'keytest'=%s\n\r", key_test.c_str());
  }

  // Serial.setDebugOutput(true);
  staGotIPHandler = WiFi.onStationModeGotIP(&onSTAGotIP);
  staDisconnectHandler = WiFi.onStationModeDisconnected(&onSTADisconnected);

  // NTP Setup
  sntp_setservername(0, "us.pool.ntp.org");
  sntp_setservername(1, "pool.ntp.org");
  sntp_init();

  ertamr::ErtAmrMqttClientConfig influxConfig;
  influxConfig.clientId = "";
  influxConfig.host = "owen-htpc.local";
  influxConfig.port = 1883;
  influxConfig.qos = 0;
  influxConfig.clientId = "";
  influxConfig.username = "telegraf";
  influxConfig.password = "telegraf";
  influxConfig.topic = "sensors/ertamr";
  influxConfig.serializer = &influxSerializer;
  influxConfig.maxQueued = 10;

  mqttInflux.configure(influxConfig);

  ertamr::ErtAmrMqttClientConfig thingsboardConfig;
  thingsboardConfig.host = "demo.thingsboard.io";
  thingsboardConfig.port = 1883;
  thingsboardConfig.clientId = "e026fda0-cecd-11e9-862b-9deda36b0cc7";
  thingsboardConfig.username = "an3rLk4odSos4Q3o3SOF";
  thingsboardConfig.password = "";
  thingsboardConfig.qos = 0;
  thingsboardConfig.topic = "v1/devices/me/telemetry";
  thingsboardConfig.serializer = &jsonSerializer;
  thingsboardConfig.maxQueued = 10;

  mqttThingsboard.configure(thingsboardConfig);

  // Enable WiFi
  WiFi.begin(ssid, password);
  // Disable WiFi
  // WiFi.disconnect();
  // WiFi.mode(WIFI_OFF);
  // WiFi.forceSleepBegin();

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  chipId = ESP.getChipId();
  Serial.printf("ESP8266 Chip ID: %u (0x%x)\r\n", chipId, chipId);

  if (mdns.begin("esp8266", WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }

  // Configure the webserver
  server = new AsyncWebServer(80);
  server->rewrite("/", "/index.html");
  server->on("/index.html", HTTP_GET, onHome);

  // 404
  server->onNotFound(
      [](AsyncWebServerRequest *request) { request->send(404); });
  server->begin();
  Serial.println("HTTP server started");

  amrInit();
  registerAmrMsgCallback(&handleAmrMsg);
  amrEnable(true);

  logESPStats();
  amrProcessing.attach(1.0, amrProcessMsgs);
  logProcessing.attach(5.0, logESPStats);
}

void loop(void) {}
