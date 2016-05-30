
/**
 * @file report_data_server.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief implement of virtual server for report data with libevent.
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

#include <memory.h>
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/platform.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/query_cache.h"
#include "zisync/kernel/libevent/ui_event_server.h"

namespace zs {

UiEventServer UiEventServer::s_instance;

UiEventServer::UiEventServer()
    : timer_event_(NULL), parse_status_(PARSE_BEGIN), is_success_(false) {
  
}

UiEventServer::~UiEventServer() {
  assert(timer_event_ == NULL);
}

err_t UiEventServer::Startup(ILibEventBase* base) {
  timer_event_ = new TimerEvent(this, true);
  timer_event_->AddToBase(GetEventBase(), REPORT_INTERVAL_IN_MS);
  report_cache_mutex_.Initialize();

  return ZISYNC_SUCCESS;
}

err_t UiEventServer::Shutdown(ILibEventBase* base) {
  report_cache_mutex_.CleanUp();
  if (timer_event_) {
    delete timer_event_;
    timer_event_ = NULL;
  }

  return ZISYNC_SUCCESS;
}

static void LambdaUiEventRead(struct bufferevent* bev, void* ctx) {
  UiEventServer* server = (UiEventServer*) ctx;
  server->UiEventRead(bev);
}

static void LambdaUiEventEvent(struct bufferevent* bev, short event, void* ctx) {
  UiEventServer* server = (UiEventServer*) ctx;
  server->UiEventEvent(bev, event);
}

void UiEventServer::OnTimer(TimerEvent* timer_event) {
  struct bufferevent* bev =
      bufferevent_socket_new(GetEventBase()->event_base(), -1, BEV_OPT_CLOSE_ON_FREE);
  assert(bev);

  bufferevent_setcb(
      bev,
      LambdaUiEventRead,
      NULL,
      LambdaUiEventEvent,
      this);
   // bufferevent_setcb(
   //   bev,
   //   [](struct bufferevent* bev, void* ctx) {
   //     UiEventServer* server = (UiEventServer*) ctx;
   //     server->UiEventRead(bev);
   // },
   //   NULL,
   //   [](struct bufferevent* bev, short event, void* ctx) {
   //     UiEventServer* server = (UiEventServer*) ctx;
   //     server->UiEventEvent(bev, event);
   // },
   //   this);

  if (bufferevent_enable(bev, EV_READ | EV_WRITE) == -1) {
    ZSLOG_ERROR("bufferevent_enable error");
  }

  if (Write(bev) == false) {
    ZSLOG_ERROR("write data into output bufer error");
    bufferevent_free(bev);
    return;
  }
  
  if(bufferevent_socket_connect_hostname(bev, GetEventBase()->evdns_base(),
                                         AF_INET, kUiEventHost, 80) == -1) {
    ZSLOG_ERROR("bufferevent_socket_connect_hostname error");
  }
}

bool UiEventServer::Write(struct bufferevent* bev) {
  if (report_cache_.size() == 0) {
    ZSLOG_INFO("report_cache_ empty");
    return false;
  }

  {
    MutexAuto mutex(&report_cache_mutex_);
    report_cache_candidate_.swap(report_cache_);
  }
  std::string actions;
  actions.append("[");
  MutexAuto mutex(&report_cache_mutex_);
  for (size_t i = 0; i < report_cache_candidate_.size(); i++) {
    if (i != 0) {
      StringAppendFormat(&actions, ", ");

    }
    StringAppendFormat(&actions, "{\"event\":\"%s\", \"time\":\"%" PRId64 "\"}",
                       report_cache_candidate_[i].name_.data(),
                       report_cache_candidate_[i].time_);
  }
  actions.append("]");

  std::string report_host;
  std::string report_uri = kReportUiEventUri;
  size_t first_pos = report_uri.find(':') + 3;
  assert(first_pos != std::string::npos);
  size_t last_pos = report_uri.rfind(':');
  assert(last_pos != std::string::npos);

  report_host = report_uri.substr(first_pos, last_pos - first_pos);


  std::string buffer;
  StringFormat(&buffer, "POST /v1/events/%s/%s/%s HTTP/1.1\r\n"
               "Host: %s\r\n" "Content-Type: text/json\r\n"
               "Content-Length: %d\r\n" "\r\n" "%s",
               Config::device_uuid().data(), GetPlatformWithString().data(),
               zisync_version.c_str(), report_host.c_str(), static_cast<int>(actions.size()),
               actions.data());
  actions.append("]");

  struct evbuffer* output = bufferevent_get_output(bev);
  assert(output);
  if (evbuffer_add(output, buffer.c_str(), buffer.size()) ==-1) {
    ZSLOG_ERROR("evbuffer_add error");
    return false;
  }
  return true;
}

bool UiEventServer::Report(std::string actionname) {
  MutexAuto mutex(&report_cache_mutex_);
  report_cache_.push_back(UIActionInfo(actionname, OsTimeInS()));
  return true;
}

void UiEventServer::UiEventRead(struct bufferevent* bev) {
  assert(bev);
  std::unique_ptr<char, decltype(free)*> line(NULL, free);
  struct evbuffer* input = bufferevent_get_input(bev); 
  assert(input);

  do {
    line.reset(evbuffer_readln(input, NULL, EVBUFFER_EOL_CRLF));
    if (line == NULL) return;

    switch (parse_status_) {
      case PARSE_BEGIN:
        if (strcmp(line.get(), "HTTP/1.1 200 OK") == 0) {
          parse_status_ = PARSE_HEAD;
        }
        break;
      case PARSE_HEAD:
        if (strcmp(line.get(), "") == 0) {
          parse_status_ = PARSE_DOUBLE_EOL;
        }
        break;
      case PARSE_DOUBLE_EOL:
        if (strcmp(line.get(), "success") == 0) {
          is_success_ = true;
        }
        break;
      default:
        break;
    }
  } while (1);
}

void UiEventServer::UiEventEvent(struct bufferevent* bev, short event) {
  //
  // connect success
  //
  if (event & BEV_EVENT_CONNECTED) {
    ZSLOG_INFO("connect %s success.", kReportHost);
    return;
  }

  if (event & BEV_EVENT_EOF) {
    if (!is_success_) {
      MutexAuto mutex(&report_cache_mutex_);
      report_cache_.swap(report_cache_candidate_);
      copy(report_cache_candidate_.begin(), report_cache_candidate_.end(),
           back_inserter(report_cache_));
    }
    ZSLOG_INFO("Connection closed.\n");
  }
  else if (event & BEV_EVENT_ERROR) {
    /* XXX win32 */
    ZSLOG_ERROR("Got an error on the connection: %s\n", strerror(errno));
  }
  /*
   * None of the other events can happen here, since we haven't
   * enabled timeouts
   */
  bufferevent_free(bev);
}


}  // namespace zs

