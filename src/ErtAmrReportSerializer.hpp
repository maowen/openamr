#pragma once

#include <ostream>
#include "ErtAmrReport.hpp"

namespace ertamr {

enum SerializerType {
  SerializerTypeJson = 0,
  SerializerTypeInfluxdb,
};

class ReportSerializer {
 public:
  ReportSerializer() {}
  virtual ~ReportSerializer() {}

  virtual SerializerType format() const = 0;
  virtual std::string serializeScmReport(const ScmReport &scm) const = 0;
  virtual std::string serializeIdmReport(const IdmReport &idm) const = 0;
  virtual std::string serializeLogReport(const LogReport &log) const = 0;
};

class ReportToInfluxdbLine : public ReportSerializer {
 public:
  SerializerType format() const override { return SerializerTypeInfluxdb; }
  std::string serializeScmReport(const ScmReport &scm) const override;
  std::string serializeIdmReport(const IdmReport &idm) const override;
  std::string serializeLogReport(const LogReport &log) const override;
};

class ReportToJson : public ReportSerializer {
 public:
  SerializerType format() const override { return SerializerTypeJson; }
  std::string serializeScmReport(const ScmReport &scm) const override;
  std::string serializeIdmReport(const IdmReport &idm) const override;
  std::string serializeLogReport(const LogReport &log) const override;
};

}  // namespace ertamr