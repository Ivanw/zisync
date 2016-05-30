// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_WORKER_INNER_REPORTMONITER_H_
#define ZISYNC_KERNEL_WORKER_INNER_REPORTMONITER_H_

#include <stdlib.h>

#include "zisync_kernel.h"
#include "zisync/kernel/platform/platform.h"

namespace zs {

class ReportMonitor;

class UIActionInfo {
 public:
  UIActionInfo(std::string name, int64_t time) {
    name_ = name;
    time_ = time;
  }
  std::string name_;
  int64_t time_;
};

// ReportMonitor* GetUIEventMonitor();

class ReportMonitor {
 public:
  virtual err_t Initialize() = 0;
  virtual err_t CleanUp() = 0;
  virtual bool Report(std::string action_name = "") = 0;
  virtual bool SendData() = 0;
};

class ReportTimerHandler : public IOnTimer {
 public:
  virtual void OnTimer();
};

class ReportUIMonitor : public ReportMonitor {
 public:
  ReportUIMonitor();
  ~ReportUIMonitor();

  virtual err_t Initialize();
  virtual err_t CleanUp();
  virtual bool Report(std::string action_name = "");
  virtual bool SendData();
  
 private:
  OsMutex report_cache_mutex_;
  std::vector<UIActionInfo> report_cache_;  
  ReportTimerHandler report_timer_handler_;
  OsTimer report_timer_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_WORKER_INNER_REPORTMONITER_H_
