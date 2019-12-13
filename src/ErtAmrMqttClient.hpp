#pragma once

extern "C" {
#include <amr.h>
}

#include <AsyncMqttClient.hpp>
#include "ErtAmrReportSerializer.hpp"

#include <deque>

namespace ertamr {

// * Convert AMR message to MQTT payload string
//   * Support multiple formats (JSON, Influx-line)
//   * Support multiple message types (Consumption, Production, Net Usage, Log
//   stats, )
// class AmrMsg2StrIface {
//     virtual String AmrScmReport2Str(const AmrScmMsg * msg) = 0;
//     virtual String AmrIdmMsg2Str(const AmrIdmMsg * msg) = 0;
//     virtual String AmrScmPluMsg2Str(const AmrIdmMsg *msg) = 0;
//     virtual String Stats
// https://github.com/marvinroger/async-mqtt-client/issues/23
// https://platformio.org/lib/show/346/AsyncMqttClient/examples?file=main.cpp

// }

//
// Jobs
// * Configurable broker addr/port user/pass
// * Maintain connection to a single MQTT broker
// * Publish MQTT messages (with retry) until they have succeeded (topic,
// string, qos, etc.)

struct ErtAmrMqttClientConfig {
  std::string clientId;
  std::string username;
  std::string password;
  std::string host;
  uint16_t port;
  std::string topic;
  uint8_t qos;
  ReportSerializer *serializer;
  uint16_t maxQueued;
};

class ErtAmrMqttClient {
 private:
  bool _configured;
  ReportSerializer *_serializer;
  AsyncMqttClient _mqttClient;
  uint8_t _qos;
  std::string _topic;
  std::deque<std::string> _queue;
  uint16_t _maxQueued;
  uint16_t _pendingPktId;
  std::string _host;
  uint16_t _port;

  void _onConnect(bool sessionPresent);
  void _onPublish(uint16_t packetId);
  int _publishQueuedMsgs();
  void _dequeueMsg();
  void _enqueueMsg(std::string msg);

 public:
  ErtAmrMqttClient();
  ~ErtAmrMqttClient() {}

  void configure(const ErtAmrMqttClientConfig &config);
  int connect();
  int publishScmReport(const ScmReport &report);
  int publishIdmReport(const IdmReport &report);
  int publishLogReport(const LogReport &report);
};

}  // namespace ertamr