#include "zisync/kernel/utils/message.h"

namespace zs {

class ReportDataHandler : public MessageHandler {
 public:
  virtual ~ReportDataHandler() {
    /* virtual desctructor */
  }
  virtual MsgCode GetMsgCode() const { return MC_REPORT_DATA_REQUEST; }
  virtual ::google::protobuf::Message* mutable_msg() { return &request_msg_; }

  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);

  bool GetStatisticDataWithJson(std::string &version, std::string& rt_type,
                                std::string* json_data);

  err_t SendStatisticData(std::string& version, std::string& rt_type,
                          int timeout_sec);
 private:
  MsgReportDataRequest request_msg_;
};

};  // namespace zs
