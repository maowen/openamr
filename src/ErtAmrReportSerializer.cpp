
#include "ErtAmrReportSerializer.hpp"

#include <string>
#include <inttypes.h>

namespace ertamr {

std::string uint32ToString(uint32_t val) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%" PRIu32, val);
  return std::string(buf);
}

std::string ReportToInfluxdbLine::serializeScmReport(
    const ScmReport &scm) const {
  std::string out;
  std::string keyName;

  // Measurement name
  switch (scm.scmType) {
    case SCM_TYPE_CONSUMPTION:
      out += "scm";
      keyName = " consumption=";
      break;
    case SCM_TYPE_PRODUCTION:
      out += "scmProd";
      keyName = " production=";
      break;
    case SCM_TYPE_NET_USAGE:
      out += "scmNet";
      keyName = " netUsage=";
      break;
    default:
      // Invalid type
      return "";
  }

  // Client Id tag
  if (scm.deviceId != "") {
    out += ",deviceId=" + scm.deviceId;
  }

  // ERT Id tag
  if (scm.ertId > 0) {
    out += ",ertId=" + uint32ToString(scm.ertId);
  }

  // Consumption / Production / Net
  out += " ";
  out += keyName + uint32ToString(scm.wattHrs);

  // Timestamp (convert from seconds to nanoseconds)
  if (scm.tstamp_s != 0) {
    out += " ";
    out += uint32ToString(scm.tstamp_s) + "000000000";
  }

  return out;
}

// TODO: Handle standard IDM message
std::string ReportToInfluxdbLine::serializeIdmReport(
    const IdmReport &idm) const {
  // Measurement name
  std::string out = "idm";

  if (idm.deviceId != "") {
    out += ",deviceId=" + idm.deviceId;
  }

  if (idm.ertId > 0) {
    out += ",ertId=" + uint32ToString(idm.ertId);
  }

  out += " consumption=" + uint32ToString(idm.consumption_Wh);
  out += ",diffConsumption=" + uint32ToString(idm.diffConsumption_Wh);
  out += ",consumptionLR=" + uint32ToString(idm.consumptionLR_Wh);
  out += ",productionLR=" + uint32ToString(idm.productionLR_Wh);
  out += ",netUsageLR=" + uint32ToString(idm.netUsageLR_Wh);
  out += ",msgCnt=" + uint32ToString(idm.msgCnt);
  out += ",txTimeOffset=" + uint32ToString(idm.txTimeOffset_ms);

  // Timestamp (convert from seconds to nanoseconds)
  if (idm.tstamp_s != 0) {
    out += " ";
    out += uint32ToString(idm.tstamp_s) + "000000000";
  }

  return out;
}

std::string ReportToInfluxdbLine::serializeLogReport(
    const LogReport &log) const {
  std::string out = "log";

  if (log.deviceId != "") {
    out += ",deviceId=" + log.deviceId;
  }

  out += " uptime=" + uint32ToString(log.uptime_s);
  out += ",freeHeap=" + uint32ToString(log.freeHeap);
  out += ",connectStatus=\"" + log.connectStatus + "\"";

  // Timestamp (convert from seconds to nanoseconds)
  if (log.tstamp_s != 0) {
    out += " ";
    out += uint32ToString(log.tstamp_s) + "000000000";
  }

  return out;
}

std::string ReportToJson::serializeScmReport(const ScmReport &scm) const {
  std::string out = "{";

  if (scm.tstamp_s != 0) {
    // Convert from seconds to milliseconds
    out += "\"ts\":" + uint32ToString(scm.tstamp_s) + "000";
    out += ", ";
    out += "\"values:\" {";
  }

  if (scm.deviceId != "") {
    out += "\"DeviceId=\":" + scm.deviceId + ", ";
  }
  // Measurement name
  switch (scm.scmType) {
    case SCM_TYPE_CONSUMPTION:
      out += "\"Consumption=\":";
      break;
    case SCM_TYPE_PRODUCTION:
      out += "\"Production=\":";
      break;
    case SCM_TYPE_NET_USAGE:
      out += "\"NetUsage=\":";
      break;
    default:
      // Invalid type
      return "";
  }

  out += uint32ToString(scm.wattHrs) + "}";

  if (scm.tstamp_s != 0) {
    out += "}";
  }

  return out;
}

std::string ReportToJson::serializeIdmReport(const IdmReport &idm) const {
  std::string out = "{";

  if (idm.tstamp_s != 0) {
    // Convert from seconds to milliseconds
    out += "\"ts\":" + uint32ToString(idm.tstamp_s) + "000";
    out += ", ";
    out += "\"values:\" {";
  }

  if (idm.deviceId != "") {
    out += "\"DeviceId\":" + idm.deviceId + ", ";
  }
  out += "\"ConsumptionHR\":" + uint32ToString(idm.consumption_Wh);
  out += ", \"DiffConsumptionHR\":" + uint32ToString(idm.diffConsumption_Wh);
  out += ", \"ConsumptionLR\":" + uint32ToString(idm.consumptionLR_Wh);
  out += ", \"ProductionLR\":" + uint32ToString(idm.productionLR_Wh);
  out += ", \"NetUsageLR\":" + uint32ToString(idm.netUsageLR_Wh);
  out += ", \"IDMMsgCnt\":" + uint32ToString(idm.msgCnt);
  out += ", \"TxTimeOffset\":" + uint32ToString(idm.txTimeOffset_ms);
  out += "}";

  if (idm.tstamp_s != 0) {
    out += "}";
  }

  return out;
}

std::string ReportToJson::serializeLogReport(const LogReport &log) const {
  std::string out = "{";

  if (log.tstamp_s != 0) {
    // Convert from seconds to milliseconds
    out += "\"ts\":" + uint32ToString(log.tstamp_s) + "000";
    out += ", ";
    out += "\"values:\" {";
  }

  if (log.deviceId != "") {
    out += "\"DeviceId\":" + log.deviceId + ", ";
  }

  out += "\"uptime\":" + uint32ToString(log.uptime_s);
  out += ", \"freeHeap\":" + uint32ToString(log.freeHeap);
  out += ", \"connectStatus\":\"" + log.connectStatus;
  out += "\"}";

  if (log.tstamp_s != 0) {
    out += "}";
  }

  return out;
}

}  // namespace ertamr