// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_MONITOR_MONITOR_H_
#define ZISYNC_KERNEL_MONITOR_MONITOR_H_

#include <map>
#include <memory>
#include <list>

#ifdef _MSC_VER
  #pragma warning( push )
  #pragma warning( disable : 4244)
  #pragma warning( disable : 4267)
  #include "zisync/kernel/proto/kernel.pb.h"
  #pragma warning( pop )
#else
  #include "zisync/kernel/proto/kernel.pb.h"
#endif

#include "zisync/kernel/platform/platform.h"
#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/monitor/fs_monitor.h"

namespace zs {

using std::map;
using std::unique_ptr;
using std::list;

class IFsMonitor;
class ZmqSocket;
class MonitorReportEventRequest;

class Reporter : public IFsReporter {
 public :
  Reporter():last_refresh_time(-1), reporter(NULL) {}
  ~Reporter();
  err_t Initialize();
  virtual int Report(const string &watch_dir, const FsEvent& fs_event);
  virtual int ReportMiss();
  virtual int ReportMiss(const string &watch_dir);
  virtual bool IsIgnored(const string &path);

  int64_t last_refresh_time;
 private:
  ZmqSocket *reporter;
};

/*  singleton */
class Monitor : public OsThread {
 public:
  ~Monitor() {
    assert(listener == NULL);
    assert(reporter == NULL);
    assert(exit_sub == NULL);
  }
  err_t Startup();
  void Shutdown();
  void Cancel();
  static Monitor* GetMonitor() {
    return &monitor;
  }
  
  err_t AddWatchDir(const string &path);
  
  err_t DelWatchDir(const string &path);

  void ReportFailBack(const string &tree_root, const MsgReportEvent &evt);
  void ReportFailBack(const string &tree_root, const FsEvent& evt);

 private:
  Monitor();
  Monitor(Monitor&);
  void operator=(Monitor&);

  class ListenerEvent {
   public:
    MsgReportEventType type;
    int64_t last_report_time_in_ms;
  };

  ZmqSocket *listener, *reporter, *exit_sub;
  IFsMonitor *fs_monitor;
  unique_ptr<Reporter> reporter_;
  map<string, map<int64_t, map<string, ListenerEvent>>> report_events;
  bool has_startup;

  virtual int Run();
  void HandleReportEventRequest();
  void ReportEventToWorker(bool *has_wait_event);
  void ReportFileMoveEvents(MonitorReportEventRequest &request,
      int64_t cookie, map<string, ListenerEvent> &events_queue, bool *has_wait_event);
  void ReportOtherEvents(MonitorReportEventRequest &request,
      map<string, ListenerEvent> &events_queue, bool *has_wait_event);
  void AddEvent(MonitorReportEventRequest &request, const ListenerEvent &evt, int64_t cookie, const string &path);

  static Monitor monitor;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_MONITOR_LISTENER_H_
