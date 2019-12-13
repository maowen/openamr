#include "ErtAmrMqttClient.hpp"

#include <Arduino.h>

ertamr::ErtAmrMqttClient::ErtAmrMqttClient()
    : _configured{false},
      _serializer{nullptr},
      _qos{0},
      _topic{""},
      _maxQueued{10},
      _pendingPktId{0},
      _host{""},
      _port{0} {}

void ertamr::ErtAmrMqttClient::configure(const ErtAmrMqttClientConfig &config) {
  if (!config.clientId.empty()) {
    _mqttClient.setClientId(config.clientId.c_str());
  }
  if (!config.username.empty()) {
    if (!config.password.empty()) {
      _mqttClient.setCredentials(config.username.c_str(),
                                 config.password.c_str());
    } else {
      _mqttClient.setCredentials(config.username.c_str());
    }
  }
  _mqttClient.setServer(config.host.c_str(), config.port);
  _mqttClient.onConnect(std::bind(&ertamr::ErtAmrMqttClient::_onConnect, this,
                                  std::placeholders::_1));
  _mqttClient.onPublish(std::bind(&ertamr::ErtAmrMqttClient::_onPublish, this,
                                  std::placeholders::_1));
  _topic = config.topic;
  _qos = config.qos;
  _serializer = config.serializer;
  _maxQueued = _maxQueued;
  _host = config.host;
  _port = config.port;

  _configured = true;
}

int ertamr::ErtAmrMqttClient::connect() {
  if (_configured) {
    Serial.printf("Attempting to connect to MQTT broker: %s:%u\n",
                  _host.c_str(), _port);
    _mqttClient.connect();
  } else {
    return -1;
  }

  return 0;
}

void ertamr::ErtAmrMqttClient::_onConnect(bool sessionPresent) {
  Serial.printf("Connected to MQTT broker: %s:%u\n", _host.c_str(), _port);
  _publishQueuedMsgs();
}

void ertamr::ErtAmrMqttClient::_onPublish(uint16_t packetId) {
  _publishQueuedMsgs();
}

int ertamr::ErtAmrMqttClient::_publishQueuedMsgs() {
  if (!_mqttClient.connected()) {
    connect();
    return 0;
  }

  auto s = std::begin(_queue);
  while (s != std::end(_queue)) {
    Serial.printf("Publishing msg: '%s'\n", s->c_str());
    uint16_t pktId = _mqttClient.publish(_topic.c_str(), _qos, false,
                                         s->c_str(), s->length());
    // Packet failed to send. Try again later
    if (pktId == 0) {
      return -1;
    }

    s = _queue.erase(s);
  }

  return 0;
}

void ertamr::ErtAmrMqttClient::_enqueueMsg(std::string msg) {
  _queue.push_back(msg);

  if (_maxQueued == 0) {
    return;
  }

  while (_queue.size() > _maxQueued) {
    _queue.pop_front();
  }
}

int ertamr::ErtAmrMqttClient::publishScmReport(const ScmReport &report) {
  if (!_configured || _serializer == nullptr) {
    return -1;
  }

  std::string msg = _serializer->serializeScmReport(report);
  if (msg.empty()) {
    return -1;
  }

  _enqueueMsg(msg);

  return _publishQueuedMsgs();
}

int ertamr::ErtAmrMqttClient::publishIdmReport(const IdmReport &report) {
  if (!_configured || _serializer == nullptr) {
    return -1;
  }

  std::string msg = _serializer->serializeIdmReport(report);
  if (msg.empty()) {
    return -1;
  }

  _enqueueMsg(msg);

  return _publishQueuedMsgs();
}

int ertamr::ErtAmrMqttClient::publishLogReport(const LogReport &report) {
  if (!_configured || _serializer == nullptr) {
    return -1;
  }

  std::string msg = _serializer->serializeLogReport(report);
  if (msg.empty()) {
    return -1;
  }

  _enqueueMsg(msg);

  return _publishQueuedMsgs();
}