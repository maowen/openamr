#pragma once

extern "C" {
#include <stdint.h>
#include <amr.h>
}

namespace ertamr {

enum ScmType {
  SCM_TYPE_CONSUMPTION = 0,
  SCM_TYPE_PRODUCTION,
  SCM_TYPE_NET_USAGE,
};

struct IdmReport {
  uint32_t tstamp_s;
  std::string deviceId;
  uint32_t ertId;
  uint32_t consumption_Wh;
  uint32_t diffConsumption_Wh;
  uint32_t consumptionLR_Wh;
  uint32_t productionLR_Wh;
  uint32_t netUsageLR_Wh;
  uint16_t msgCnt;
  uint16_t txTimeOffset_ms;
};

struct ScmReport {
  uint32_t tstamp_s;
  std::string deviceId;
  uint32_t ertId;
  uint32_t wattHrs;
  ScmType scmType;
};

struct LogReport {
  uint32_t tstamp_s;
  std::string deviceId;
  uint32_t uptime_s;
  uint32_t freeHeap;
  std::string connectStatus;
};

}  // namespace ertamr