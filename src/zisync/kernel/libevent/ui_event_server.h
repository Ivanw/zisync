/**
 * @file report_data_server.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief virtual server for report data.
 *
 * Copyright (C) 2009 Likun Liu <liulikun@gmail.com>
 * Free Software License:
 *
 * All rights are reserved by the author, with the following exceptions:
 * Permission is granted to freely reproduce and distribute this software,
 * possibly in exchange for a fee, provided that this copyright notice appears
 * intact. Permission is also granted to adapt this software to produce
 * derivative works, as long as the modified versions carry this copyright
 * notice and additional notices stating that the work has been modified.
 * This source code may be translated into executable form and incorporated
 * into proprietary software; there is no requirement for such software to
 * contain a copyright notice related to this source.
 *
 * $Id: $
 * $Name: $
 */

#ifndef ZISYNC_KERNEL_LIBEVENT_UI_EVENT_SERVER_H_
#define ZISYNC_KERNEL_LIBEVENT_UI_EVENT_SERVER_H_

#include "zisync/kernel/libevent/libevent++.h"
#include "zisync/kernel/platform/platform.h"

namespace zs {

class UIActionInfo {
 public:
  UIActionInfo(std::string name, int64_t time) {
    name_ = name;
    time_ = time;
  }
  std::string name_;
  int64_t time_;
};

class UiEventServer : public ILibEventVirtualServer
                    , public ITimerEventDelegate {
 public:
  UiEventServer();
  virtual ~UiEventServer();

  //
  // Implement ILibEventVirtualServer
  //
  virtual err_t Startup(ILibEventBase* base);
  virtual err_t Shutdown(ILibEventBase* base);
  //
  // Implement  ITimerEventDelegate
  //
  virtual void OnTimer(TimerEvent* timer_event);

  bool Write(struct bufferevent* bev);
  bool Report(std::string action_name = "");

  void UiEventRead(struct bufferevent* bev);
  void UiEventEvent(struct bufferevent* bev, short event);

 public:
  static UiEventServer* GetInstance() {
    return &s_instance;
  }

 protected:
  static UiEventServer s_instance;

 protected:
  TimerEvent* timer_event_;
 private:
  enum parse_status { PARSE_BEGIN, PARSE_HEAD, PARSE_DOUBLE_EOL };
  OsMutex report_cache_mutex_;
  std::vector<UIActionInfo> report_cache_;
  std::vector<UIActionInfo> report_cache_candidate_;
  int parse_status_;
  bool is_success_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_LIBEVENT_REPORT_DATA_SERVER_H_
