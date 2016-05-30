/**
 * @file libevent_base.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief implmentation of ILibEventBase.
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

#ifndef LIBEVENT_BASE_H
#define LIBEVENT_BASE_H

#include <openssl/ssl.h>
#include <vector>
#include <memory>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/libevent/libevent++.h"

namespace zs {

class OsWorkerThread;

class LibEventBase : public ILibEventBase
                   , public IRunnable
                   , public IUserEventDelegate {
 public:
  LibEventBase();
  virtual ~LibEventBase();

  //
  // Implement ILibEventBase
  //
  virtual void RegisterVirtualServer(ILibEventVirtualServer* server);
  virtual void UnregisterVirtualServer(ILibEventVirtualServer* server);

  virtual err_t Startup();
  virtual err_t Shutdown();

  virtual err_t DispatchSync(dispatch_sync_func_t dispatch_block, void* ctx);
  virtual void DispatchAsync(
      dispatch_async_func_t dispatch_block, void* ctx, struct timeval* tv);

  //
  // Implement IRunnable
  //
  virtual int Run();
  //
  // Implement IUserEventDelegate
  //
  virtual void OnUserEvent(UserEvent* event, short events);
  
  struct event_base* event_base() {
    return event_base_;
  }
  
  struct evdns_base* evdns_base() {
    return evdns_base_;
  }

 protected:
  void ThreadStartup();
  void ThreadShutdown();

 protected:
  struct event_base* event_base_;
  struct evdns_base* evdns_base_;

  OsWorkerThread* thread_;
  std::unique_ptr<OsEvent>  event_started_;

  OsMutex dispatch_mutex_;
  UserEvent* dispatch_event_;
  std::vector<std::shared_ptr<IRunnable>> dispatch_queue; 
  
  std::vector<ILibEventVirtualServer*> virtual_servers_;

  friend class LibEventBaseTester;
};

typedef enum {
  EV_HTTP_OK = 0,
  EV_HTTP_REQ_NULL,
  EV_HTTP_ERROR_CODE,
  EV_HTTP_ERROR_CONTENT,

  EV_HTTP_NUM,
}evhttp_err_t;

class HttpRequest : public IHttpRequest {
 public:
  HttpRequest(void(*cb)(struct evhttp_request *, void *),
       const std::string &uri, enum evhttp_cmd_type type,
              void (*func)(const std::string&, const std::string&, const std::string&, evhttp_err_t),
              const std::string &method, const std::string &data = std::string());
  ~HttpRequest();

  virtual err_t AddHeader(
      const std::string &key, const std::string &value);
  virtual err_t AddContent(const std::string &content);
  virtual err_t RemoveHeader(const std::string &header);

  virtual err_t SendRequest(struct evhttp_connection *evcon);
  virtual err_t HandleRequest(struct evhttp_request *req);
  virtual void SetBev(struct bufferevent *bev) {
    bev_ = bev;
  }

  void HandleTimeOut();
//  virtual std::string host();
//  virtual std::string uri();
//  virtual std::string fragment();
//  virtual std::string path();
//  virtual std::string port();
//  virtual std::string scheme();
//  virtual std::string userinfo();
  
 private:
  struct evhttp_request *req_;
  std::string uri_;
  enum evhttp_cmd_type type_;

  std::string method_;
  void (*handle_func_)(const std::string&, const std::string&, const std::string &, evhttp_err_t);
  struct bufferevent *bev_;
  
  struct event *timed_cancel_ev_;
  string custom_data_;
};
  
}

#endif
