/**
 * @file libevent++.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief libevent interface.
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

#ifndef ZISYNC_KERNEL_LIBEVENT_LIBEVENT___H_
#define ZISYNC_KERNEL_LIBEVENT_LIBEVENT___H_

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/dns.h>
#include <event2/http.h>

#include "zisync_kernel.h"  // NOLINT

#define MAX_OUTSTANDING_SIZE (64 * 1024)

namespace zs {

class TimerEvent;
class BufferEvent;
class UserEvent;
class ILibEventBase;

class ITimerEventDelegate {
 public:
  virtual ~ITimerEventDelegate() { /* virtual destructor */ }
  
  virtual void OnTimer(TimerEvent* timer_event) = 0;
};

class TimerEvent {
 public:
  TimerEvent(ITimerEventDelegate* delegate, bool is_persist);
  virtual ~TimerEvent();

  virtual void AddToBase(ILibEventBase* event_base,
                         int32_t interval_in_ms);

 protected:
  static void LambdaOnTimer(evutil_socket_t fd, short events, void* ctx);

  bool is_persist_;
  struct event* event_;
  ITimerEventDelegate* delegate_;  
};

class IListenEventDelegate {
 public:
  virtual ~IListenEventDelegate() { }
  
  virtual void OnAccept(struct evconnlistener *listener,
                        evutil_socket_t socket,
                        struct sockaddr *sockaddr, int socklen) = 0;

};

class IDnsEventDelegate {
 public:
  virtual ~IDnsEventDelegate() { /* virtual destructor */ }

  virtual void OnDnsEvent(int result, evutil_addrinfo* addr) = 0;
};

class DnsEvent {
 public:
  DnsEvent(IDnsEventDelegate* delegate,
           const std::string& nodename,
           const std::string& servname,
           const evutil_addrinfo& hints);
  virtual ~DnsEvent();

  virtual void Cancel();
  virtual void AddToBase(ILibEventBase* event_base);

 protected:
  static void LambdaOnDnsEvent(int result, struct evutil_addrinfo* res, void* ctx);
  IDnsEventDelegate* delegate_;
  std::string nodename_;
  std::string servname_;
  evutil_addrinfo hints_;
  struct evdns_getaddrinfo_request* request_;
};

template <typename T>
void LambdaOnRead(struct bufferevent* bev, void* ctx) {
  reinterpret_cast<T*>(ctx)->OnRead(bev);
}

template <typename T>
void LambdaOnWrite(struct bufferevent* bev, void* ctx) {
  reinterpret_cast<T*>(ctx)->OnWrite(bev);
}

template <typename T>
void LambdaOnEvent(
    struct bufferevent *bev, short what, void *ctx) {
	reinterpret_cast<T*>(ctx)->OnEvent(bev, what);
}

class IBufferEventDelegate {
 public:
  virtual ~IBufferEventDelegate() {  }

  virtual void OnRead(struct bufferevent* bev) = 0;
  virtual void OnWrite(struct bufferevent* bev) = 0;
  virtual void OnEvent(struct bufferevent* bev, short events) = 0;

};

class BufferEvent {
 public:
  BufferEvent(IBufferEventDelegate* delegate, int fd);
  ~BufferEvent();

  virtual void AddToBase(ILibEventBase* event_base,
                         int32_t timeout_in_ms = -1);

 protected:
  struct bufferevent* bev_;
  IBufferEventDelegate* delegate_;
};

class IUserEventDelegate {
 public:
  virtual ~IUserEventDelegate() {  }

  virtual void OnUserEvent(UserEvent* event, short events) = 0;
};

class UserEvent {
 public:
  UserEvent(IUserEventDelegate* delegate, const char* name);
  ~UserEvent();

  void AddToBase(ILibEventBase* event_base, int32_t timeout_in_ms = -1);
  void Active(short events);

  const std::string& name() {
    return name_;
  }

 private:
  static void LambdaOnUserEvent(evutil_socket_t fd, short events, void *user_data);

  struct event* event_;
  IUserEventDelegate* delegate_;
  std::string name_;
};

class ILibEventVirtualServer {
 public:
  virtual ~ILibEventVirtualServer() { /* virtual destructor */ }

  virtual err_t Startup(ILibEventBase* base) = 0;
  virtual err_t Shutdown(ILibEventBase* base) = 0;

};

typedef err_t (*dispatch_sync_func_t)(void* ctx);
typedef void (*dispatch_async_func_t)(void* ctx);

class ILibEventBase {
 public:
  virtual ~ILibEventBase() { /* virtual destructor */  }

  virtual void RegisterVirtualServer(ILibEventVirtualServer* server) = 0;
  virtual void UnregisterVirtualServer(ILibEventVirtualServer* server) = 0;
  
  virtual err_t Startup() = 0;
  virtual err_t Shutdown() = 0;

  virtual err_t DispatchSync(dispatch_sync_func_t dispatch_block, void* ctx) = 0;
  virtual void DispatchAsync(
      dispatch_async_func_t dispatch_block, void* ctx, struct timeval* tv) = 0;

  virtual struct event_base* event_base() = 0;
  virtual struct evdns_base* evdns_base() = 0;
};

ILibEventBase* GetEventBase();
ILibEventBase* GetEventBaseDb();

int evutil_shutdown_socket(evutil_socket_t fd, const char* mode);

class IHttpRequest{
 public:
  virtual ~IHttpRequest() { /* virtual destructor */ }
  virtual err_t AddHeader(
      const std::string &key, const std::string &header) = 0;
  virtual err_t AddContent(const std::string &content) = 0;

  virtual err_t RemoveHeader(const std::string &header) = 0;

  virtual err_t SendRequest(struct evhttp_connection *evcon) = 0;
  virtual err_t HandleRequest(struct evhttp_request *req) = 0;
  virtual void SetBev(struct bufferevent *bev) = 0;
//
//  virtual std::string host() = 0;
//  virtual std::string uri() = 0;
//  virtual std::string fragment() = 0;
//  virtual std::string path() = 0;
//  virtual std::string port() = 0;
//  virtual std::string scheme() = 0;
//  virtual std::string userinfo() = 0;
};



}  // end namespace zs

#endif  // ZISYNC_KERNEL_LIBEVENT_LIBEVENT___H_
