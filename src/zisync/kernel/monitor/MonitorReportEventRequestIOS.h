#ifndef ZISYNC_KERNEL_MONITOR_MonitorReportEventRequestIOS_H
#define ZISYNC_KERNEL_MONITOR_MonitorReportEventRequestIOS_H
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/monitor/fsevent_report_request_ios.pb.h"
namespace zs {
    using std::string;
    class ZmqMsg;
    class ZmqSocket;
    //xcode complains that shareContext cannot be linked unless i put the declaration here
    class ZmqContext;
    const ZmqContext *shareContext(const ZmqContext *);
    extern const char fs_monitor_req_uri[];
    class MonitorReportEventRequestIOS: public Request{
        public:
            MonitorReportEventRequestIOS(){}
            virtual ~MonitorReportEventRequestIOS(){}
            virtual ::google::protobuf::Message * mutable_msg() { return &request_msg_; }
            MsgMonitorReportEventRequestIOS *mutable_request(){ return &request_msg_; }
            const MsgMonitorReportEventRequestIOS *request(){return &request_msg_;}
            virtual MsgCode msg_code() const { return MC_MONITOR_REPORT_EVENT_REQUEST; }
        private:
            MonitorReportEventRequestIOS(MonitorReportEventRequestIOS&);
            void operator=(MonitorReportEventRequestIOS&);

            MsgMonitorReportEventRequestIOS request_msg_;
    };
}
#endif
