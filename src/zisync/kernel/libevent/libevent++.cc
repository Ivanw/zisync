/**
 * @file libevent++.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief libevent wrapper.
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

#include <assert.h>
#include <string.h>

#include "zisync/kernel/libevent/libevent++.h"
#include "zisync/kernel/zslog.h"

namespace zs {

TimerEvent::TimerEvent(ITimerEventDelegate* delegate, bool is_persist)
    : is_persist_(is_persist), event_(NULL), delegate_(delegate) {
}

TimerEvent::~TimerEvent() {
  if (event_) {
    event_free(event_);
    event_= NULL;
  }
}

void TimerEvent::LambdaOnTimer(evutil_socket_t fd, short events, void* ctx) {
  assert(fd == -1);
  TimerEvent* event = (TimerEvent*) ctx;
  event->delegate_->OnTimer(event);
}

void TimerEvent::AddToBase(ILibEventBase* event_base,
                           int32_t interval_in_ms) {
  struct timeval timeout = {
    interval_in_ms / 1000,
    (interval_in_ms % 1000) * 1000
  };

  int flags = is_persist_ ? EV_PERSIST : 0;

  // event_ = event_new(event_base->event_base(), -1, flags,
  //                    [](evutil_socket_t fd, short events, void *user_data) {
  //                      assert(fd == -1);
  //                      TimerEvent* event = (TimerEvent*) user_data;
  //                      event->delegate_->OnTimer(event);
  //                    }, this);
  event_ = event_new(event_base->event_base(), -1, flags, LambdaOnTimer, this);

  event_add(event_, interval_in_ms == -1 ? NULL : &timeout);
}

DnsEvent::DnsEvent(IDnsEventDelegate* delegate,
                   const std::string& nodename,
                   const std::string& servname,
                   const evutil_addrinfo& hints)
    : delegate_(delegate), nodename_(nodename)
    , servname_(servname), hints_(hints), request_(NULL) {
}

DnsEvent::~DnsEvent() {
  // no need to free request_
}

void DnsEvent::Cancel() {
  if (request_) {
    evdns_getaddrinfo_cancel(request_);
  }
}

void DnsEvent::LambdaOnDnsEvent(
    int result, struct evutil_addrinfo* res, void* ctx) {
  DnsEvent* event = (DnsEvent*) ctx;
  event->request_ = NULL;
  event->delegate_->OnDnsEvent(result, res);
}
void DnsEvent::AddToBase(ILibEventBase* event_base) {
  // request_ = evdns_getaddrinfo(
  //     event_base->evdns_base(), nodename_.c_str(),
  //     servname_.c_str(), &hints_,
  //     [](int result, struct evutil_addrinfo* res, void *user_data) {
  //       DnsEvent* event = (DnsEvent*) user_data;
  //       event->request_ = NULL;
  //       event->delegate_->OnDnsEvent(result, res);
  //     }, this);
  request_ = evdns_getaddrinfo(
    event_base->evdns_base(), nodename_.c_str(),
    servname_.c_str(), &hints_,
    LambdaOnDnsEvent, this);
  if (request_ == NULL) {
    ZSLOG_INFO("evdns_getaddrinfo returned immediately");
  }
}

UserEvent::UserEvent(IUserEventDelegate* delegate, const char* name)
    : event_(NULL), delegate_(delegate), name_(name) {
}

UserEvent::~UserEvent() {
  if (event_) {
    event_free(event_);
    event_ = NULL;
  }
}

void UserEvent::LambdaOnUserEvent(
  evutil_socket_t fd, short events, void *user_data) {
  assert(fd == -1);
  UserEvent* event = (UserEvent*) user_data;
  event->delegate_->OnUserEvent(event, events);
}

void UserEvent::AddToBase(ILibEventBase* event_base, int32_t timeout_in_ms /* = -1 */) {
  struct timeval timeout = {
    timeout_in_ms / 1000,
    (timeout_in_ms % 1000) * 1000
  };

  event_ = event_new(event_base->event_base(), -1, EV_TIMEOUT|EV_READ|EV_WRITE,
                     LambdaOnUserEvent, this);

  event_add(event_, timeout_in_ms == -1 ? NULL : &timeout);
}

void UserEvent::Active(short events) {
  assert(event_);
  event_active(event_, events, -1);
}

int evutil_shutdown_socket(evutil_socket_t fd, const char *mode) {
#ifndef _WIN32
  if (strcmp(mode, "r") == 0) {
    return shutdown(fd, SHUT_RD);
  } else if (strcmp(mode, "w") == 0) {
    return shutdown(fd, SHUT_WR);
  } else if (strcmp(mode, "rw") == 0) {
    return shutdown(fd, SHUT_RDWR);
  }
#else
  if (strcmp(mode, "r") == 0) {
    return shutdown(fd, SD_RECEIVE);
  } else if (strcmp(mode, "w") == 0) {
    return shutdown(fd, SD_SEND);
  } else if (strcmp(mode, "rw") == 0) {
    return shutdown(fd, SD_BOTH);
  }
#endif  // _WIN32
  return -1;
}

}  // namespace zs
