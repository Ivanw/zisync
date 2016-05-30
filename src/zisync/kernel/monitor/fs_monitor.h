// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_MONITOR_FS_MONITOR_H_
#define ZISYNC_KERNEL_MONITOR_FS_MONITOR_H_

#include <string>
#include "zisync/kernel/proto/kernel.pb.h"
#include <stdint.h>

namespace zs {

enum FsEventType {
  FS_EVENT_TYPE_CREATE   = 0,
  FS_EVENT_TYPE_ATTRIB   = 1,
  FS_EVENT_TYPE_MODIFY   = 2,
  FS_EVENT_TYPE_DELETE   = 3,
  FS_EVENT_TYPE_MOVE_FROM = 4,
  FS_EVENT_TYPE_MOVE_TO   = 5,
};

class FsEvent {
 public:
  FsEvent(){}
  FsEvent(FsEventType type_, std::string path_, int64_t cookie_)
    : type(type_), path(path_), file_move_cookie(cookie_) {}
  FsEventType type;
  std::string path;
  int64_t file_move_cookie;
};
FsEventType ToFsEventType(MsgReportEventType type);
MsgReportEventType ToMsgReportEventType(FsEventType type);
class IFsReporter {
 public:
  virtual ~IFsReporter() {}
  virtual bool IsIgnored(const std::string &path) = 0;
  virtual int Report(const std::string &watch_dir, const FsEvent& fs_event) = 0;
  /*  when the inotify found that some inotfy events have been missed, call this
   *  func to inform the kernel */
  virtual int ReportMiss() = 0;
  virtual int ReportMiss(const std::string &watch_dir) = 0;
};

/*  the FsMonitor should implemeted as a thread */
class IFsMonitor {
 public:
  virtual ~IFsMonitor() {}
  /*  when FsMonitor found a new dir or file created, modified or deleted, call 
   *  reporter.Report(). 
   *  reporter.Report is not thread safe, you should call this only in one
   *  thread*/
  virtual int Startup(IFsReporter* reporter) = 0;
  virtual int Shutdown() = 0;
  virtual int AddWatchDir(const std::string& dir_path) = 0;
  virtual int DelWatchDir(const std::string& dir_path) = 0;
};

// Singleton
IFsMonitor* GetFsMonitor();

}  // namespace zs

#endif  // ZISYNC_KERNEL_MONITOR_FS_MONITOR_H_
