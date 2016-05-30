/**
 * @file libevent_base.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief implment of ILibEventBase.
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

#include <event2/dns.h>
#include <event2/thread.h>
#include <event2/http.h>
#include <event2/bufferevent_ssl.h>
#include <algorithm>
#include <openssl/err.h>
#include <tuple>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/zslog.h"
#include "libevent_base.h"
#include "zisync/kernel/libevent/report_data_server.h"
#include "zisync/kernel/libevent/ui_event_server.h"
#include "zisync/kernel/utils/base64.h"
#include "zisync/kernel/sync_const.h"

namespace zs {

static LibEventBase s_libevent_base;
static LibEventBase s_libevent_base_db;

ILibEventBase* GetEventBase() {
  return &s_libevent_base;
}

ILibEventBase* GetEventBaseDb() {
  return &s_libevent_base_db;
}

LibEventBase::LibEventBase()
    : event_base_(NULL), evdns_base_(NULL)
    , thread_(NULL), dispatch_event_(NULL) {
  if (dispatch_mutex_.Initialize() != 0) {
    ZSLOG_ERROR("failed to initialize dispatch_mutex");
    assert(false);
  }

}

LibEventBase::~LibEventBase() {
  assert(event_base_ == NULL);
  assert(evdns_base_ == NULL);

  assert(thread_ == NULL);
  dispatch_mutex_.CleanUp();
}

void LibEventBase::RegisterVirtualServer(ILibEventVirtualServer* server) {
  assert(server);
  auto iter = std::find(virtual_servers_.begin(), virtual_servers_.end(), server);
  if (iter == virtual_servers_.end()) {
    virtual_servers_.push_back(server);
  }
}

void LibEventBase::UnregisterVirtualServer(ILibEventVirtualServer* server) {
  assert(server);
  auto iter = std::find(virtual_servers_.begin(), virtual_servers_.end(), server);
  if (iter != virtual_servers_.end()) {
    virtual_servers_.erase(iter);
  }
}


err_t LibEventBase::Startup() {
  event_started_.reset(new OsEvent);
  if (event_started_ == NULL) {
    ZSLOG_ERROR("Out of memory: allocating OsEvent");
    return ZISYNC_ERROR_MEMORY;
  }

  if (event_started_->Initialize(false) != 0) {
    assert(false);
    ZSLOG_ERROR("failed OsEvent::Initialize()");
    return ZISYNC_ERROR_OS_EVENT;
  }

  thread_ = new OsWorkerThread("libevent", this, false);
  assert(thread_ != NULL);

  int ret = thread_->Startup();
  if (ret != 0) {
    assert(false);
    ZSLOG_ERROR("start thread fail");
    return ZISYNC_ERROR_OS_THREAD;
  }

  event_started_->Wait();

  return ZISYNC_SUCCESS;
}

static void LambdaAsyncShutdown(void* ctx) {
  // ReportDataServer::GetInstance()->SendReport("stop");
  OsMutex *pdispatch_mutex_;
  struct event_base *event_base_;
  std::vector<std::shared_ptr<IRunnable>> *pdispatch_queue;
  std::tie(event_base_, pdispatch_mutex_, pdispatch_queue) = *(std::tuple<struct event_base*, OsMutex*, std::vector<std::shared_ptr<IRunnable>>*>*)ctx;

  std::vector<std::shared_ptr<IRunnable>> queue;
  if (pdispatch_queue){
    MutexAuto mutex_auto(pdispatch_mutex_);
    queue.swap(*pdispatch_queue);
  }
  
  IAbortable *abort = NULL;
  for (auto it = queue.begin(); it != queue.end(); ++it) {
    std::shared_ptr<IRunnable> dispatch_block = *it;
    if (dispatch_block) {
      abort = (IAbortable*)dispatch_block.get();
      abort->Abort();
    }
  }
  
  event_base_loopbreak(event_base_);

  delete (std::tuple<struct event_base*, OsMutex*, std::vector<std::shared_ptr<IRunnable>>* >*)ctx;
  // struct timeval tv = {1, 0};
  // event_base_loopexit(event_base, &tv);
}

err_t LibEventBase::Shutdown() {
  auto ctx = new std::tuple<struct event_base*, OsMutex*, std::vector<std::shared_ptr<IRunnable>>*>(event_base_, &dispatch_mutex_, &dispatch_queue);
  DispatchAsync(LambdaAsyncShutdown, ctx, NULL);
  
  if (thread_ != NULL) {
    int ret = thread_->Shutdown();
    if (ret != 0) {
      ZSLOG_ERROR("shutdown libevent worker failed");
    }
    delete thread_;
    thread_ = NULL;
  }

  return ZISYNC_SUCCESS;
}

err_t LibEventBase::DispatchSync(
    dispatch_sync_func_t dispatch_block, void* ctx) {
  //
  // context used do do sync dispatch
  //
  class SyncBlock : public IRunnable, public IAbortable {
   public:
    err_t result_;
    OsEvent event_;
    dispatch_sync_func_t dispatch_block_;
    void* context_;
    
   public:
    SyncBlock(dispatch_sync_func_t dispatch_block, void* ctx)
        : result_(ZISYNC_SUCCESS)
        , dispatch_block_(dispatch_block)
        , context_(ctx) {
      int rc = event_.Initialize(false);
      if (rc != 0) {
        ZSLOG_ERROR("failed to init OsEvent");
        assert(0);
      }
    }
    virtual ~SyncBlock() {
      event_.CleanUp();
    }

    virtual int Run() {
      result_ = dispatch_block_(context_);
      event_.Signal();
      return 0;
    }
    
    virtual bool IsAborted(){return false;}
    virtual bool Abort() {
      result_ = ZISYNC_ERROR_CANCEL;
      event_.Signal();
      return true;
    }
  }; 

  auto block = new SyncBlock(dispatch_block, ctx);
  std::shared_ptr<IRunnable> runnable(block);
  
  {
    MutexAuto mutex_auto(&dispatch_mutex_);
    if (event_base_) {
      dispatch_queue.push_back(runnable);
      dispatch_event_->Active(EV_READ);
    } else {
      block->result_ = ZISYNC_ERROR_LIBEVENT;
      ZSLOG_WARNING("event base not init or has destrory, DispatchSync ignored");
      block->event_.Signal();
    }
  }

  block->event_.Wait();
  return block->result_;
}


void LibEventBase::DispatchAsync(
    dispatch_async_func_t dispatch_block, void* ctx, struct timeval* tv) {
  //
  // Context used to do async dispatch
  //
  class AsyncBlock : public IRunnable {
   public:
    void* context_;
    dispatch_async_func_t dispatch_block_;

   public:
    AsyncBlock(dispatch_async_func_t dispatch_block, void* ctx)
        : context_(ctx), dispatch_block_(dispatch_block) {
    }

    virtual ~AsyncBlock() {
    }

    virtual int Run() {
      dispatch_block_(context_);
      return 0;
    }

    static void LambdaAsync(
      evutil_socket_t /* fd */, short /* events */, void* ctx) {
      auto runnable = reinterpret_cast<IRunnable*>(ctx);
      runnable->Run();
      delete runnable;
    }
  };

  IRunnable* block = new AsyncBlock(dispatch_block, ctx);
  
  {
    MutexAuto auto_mutex(&dispatch_mutex_);
    if (event_base_ != NULL) {
      event_base_once(event_base_, -1, EV_TIMEOUT,
        AsyncBlock::LambdaAsync, block, tv);
    } else {
      ZSLOG_WARNING(
        "event base not init or has destroyed, dispatch async ignored");
    }
  }
}


void LibEventBase::ThreadStartup() {
  // Startup virtual servers
  for (auto it = virtual_servers_.begin(); it != virtual_servers_.end(); ++it) {
    (*it)->Startup(this);
  }
}

void LibEventBase::ThreadShutdown() {
  // Shutdown all virtual servers
  for (auto it = virtual_servers_.begin(); it != virtual_servers_.end(); ++it) {
    (*it)->Shutdown(this);
  }
}

/*
void eventlog(int iswarning, const char *msg) {
    ZSLOG_ERROR("DEBUG:LIBEVENT :%s", msg);
}
*/

int LibEventBase::Run() {
#ifdef _WIN32
  WSADATA wsa_data;
  WSAStartup(0x0201, &wsa_data);
#endif
 
  {
    MutexAuto auto_mutex(&dispatch_mutex_);
#ifdef _WIN32
    evthread_use_windows_threads();
#else
    evthread_use_pthreads();
#endif
    event_base_ = event_base_new();
    if (!event_base_) {
      ZSLOG_ERROR("Could not initialize libevent!");
      return ZISYNC_ERROR_LIBEVENT;
    }

#if defined (__ANDROID__)
    evdns_base_ = evdns_base_new(event_base_, 0);
#else
    evdns_base_ = evdns_base_new(event_base_, 1);
#endif
    if (!evdns_base_) {
      ZSLOG_ERROR("Could not initialize libevent!");
      return ZISYNC_ERROR_LIBEVENT;
    }

    evdns_base_set_option(evdns_base_, "randomize-case:", "0"); // turn off DNS-0x20 dncoding
    evdns_base_nameserver_ip_add(evdns_base_, "8.8.8.8");
    //evdns_set_log_fn(eventlog);

    if (evthread_make_base_notifiable(event_base_) < 0) {
      printf("Couldn't make base notifiable!");
      return ZISYNC_ERROR_LIBEVENT;
    }


    dispatch_event_ = new UserEvent(this, "dispatch");
    dispatch_event_->AddToBase(this, -1);  
  }

  //
  // Now, foreach virtual server call startup()
  //
  ThreadStartup();

  // notify main thread we have done initialize.
  event_started_->Signal();
  
  //
  // event loop
  //
  /*event_base_loop(event_base_, EVLOOP_NO_EXIT_ON_EMPTY);*/
  event_base_dispatch(event_base_);

  //
  // Now, we are going to exit, foreach virtual server call shutdown()
  //
  ThreadShutdown();

  {
    MutexAuto auto_mutex(&dispatch_mutex_);

    if (dispatch_event_) {
      delete dispatch_event_;
      dispatch_event_ = NULL;
    }

    if (evdns_base_) {
      evdns_base_free(evdns_base_, 1);
      evdns_base_ = NULL;
    }

    if (event_base_) {
      event_base_free(event_base_);
      event_base_ = NULL;
    }
  }


#ifdef _WIN32
  WSACleanup();
#endif
  
  return 0;
}

void LibEventBase::OnUserEvent(UserEvent* event, short events) {
  std::vector<std::shared_ptr<IRunnable>> queue;
  {
    MutexAuto mutex_auto(&dispatch_mutex_);
    queue.swap(dispatch_queue);
  }
  
  if (event->name() == "exit") {
    for (auto it = queue.begin(); it != queue.end(); ++it) {
      std::shared_ptr<IRunnable> dispatch_block = *it;
      IAbortable *abort = NULL;
      if (dispatch_block) {
        abort = (IAbortable*)dispatch_block.get();
        abort->Abort();
      }
    }
    event_base_loopbreak(event_base_);
  }
  else {
    for (auto it = queue.begin(); it != queue.end(); ++it) {
      std::shared_ptr<IRunnable> dispatch_block = *it;
      if (dispatch_block) {
        dispatch_block->Run();
      }
    }
  }
}

HttpRequest::HttpRequest(
    void (*cb)(struct evhttp_request*, void*),
    const std::string &uri, enum evhttp_cmd_type type,
                         void (*func)(const std::string&, const std::string&, const std::string&, evhttp_err_t),
                         const std::string &method, const std::string &data) :
    req_(NULL), uri_(uri), type_(type), method_(method), handle_func_(func),
    timed_cancel_ev_(NULL), custom_data_(data){
  req_ = evhttp_request_new(cb, this);
  if (req_ == NULL) {
    ZSLOG_ERROR("Create request fail.");
    assert(0);
  }
}

HttpRequest::~HttpRequest() {

}

err_t HttpRequest::AddHeader(const std::string &key, const std::string &value) {
  struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req_);
  if (evhttp_add_header(output_headers, key.c_str(), value.c_str()) == -1) {
    ZSLOG_ERROR("Add header(%s : %s) fail.", key.c_str(), value.c_str());
    return ZISYNC_ERROR_HTTP_RETURN_ERROR;
  }

  return ZISYNC_SUCCESS;
}

err_t HttpRequest::AddContent(const std::string &content) {
  struct evbuffer *output = evhttp_request_get_output_buffer(req_);
  evbuffer_add(output, content.c_str(), content.size());

  return ZISYNC_SUCCESS;
}

err_t HttpRequest::RemoveHeader(const std::string &key) {
  struct evkeyvalq *output_headers = evhttp_request_get_output_headers(req_);
  if (evhttp_remove_header(output_headers, key.c_str()) == -1) {
    ZSLOG_ERROR("Remove header(%s) fail.", key.c_str());
    return ZISYNC_ERROR_HTTP_RETURN_ERROR;
  }

  return ZISYNC_SUCCESS;
}
  
#include <event2/event.h>
  static void http_req_timeout_handler(evutil_socket_t fd, short event, void *ctx) {
    HttpRequest *http_req = (HttpRequest*)ctx;
    http_req->HandleTimeOut();
    delete http_req;
    //evhttp_cancel_request(hc->req);
    //evhttp_connection_free(hc->conn);
  }

err_t HttpRequest::SendRequest(struct evhttp_connection *evcon) {
  timed_cancel_ev_ = event_new(GetEventBase()->event_base(), -1, 0, http_req_timeout_handler, this);
  struct timeval tv;
  tv.tv_sec = HTTP_CONNECT_TIMEOUT;
  tv.tv_usec = 0;
  event_add(timed_cancel_ev_, &tv);
  if (evhttp_make_request(evcon, req_, type_, uri_.c_str()) == -1) {
    ZSLOG_ERROR("Send http request fail.");
    return ZISYNC_ERROR_HTTP_RETURN_ERROR;
  }

  return ZISYNC_SUCCESS;
}

static const char *get_ssl_error_string() {
  return ERR_error_string(ERR_get_error(), NULL);
}

err_t HttpRequest::HandleRequest(struct evhttp_request *req) {
  if (timed_cancel_ev_) {
    int ev_ret = event_del(timed_cancel_ev_);
    if (ev_ret) {
      ZSLOG_ERROR("Cannot cancel timeout event.");
    }
    event_free(timed_cancel_ev_);
    timed_cancel_ev_ = NULL;
  }
  
  if (req == NULL) {
    unsigned long oslerr = 0;
    int errcode = EVUTIL_SOCKET_ERROR();
    bool is_openssl_error = false;
    while ((oslerr = bufferevent_get_openssl_error(bev_))) {
      ZSLOG_ERROR("openssl is error: %s", get_ssl_error_string());
      is_openssl_error = true;
    }
    if (!is_openssl_error) {
      ZSLOG_ERROR("evhttp_requst is error: %s",
                  evutil_socket_error_to_string(errcode));
    } 
    handle_func_("error", method_, custom_data_, EV_HTTP_REQ_NULL);
    return ZISYNC_ERROR_HTTP_RETURN_ERROR;
  }
  struct evkeyvalq *input_headers = evhttp_request_get_input_headers(req);
  struct evbuffer *input_buffer = evhttp_request_get_input_buffer(req);
  int code = evhttp_request_get_response_code(req);
  if (code == HTTP_OK) {
    ZSLOG_INFO("Successfull.");
  } else {
    ZSLOG_ERROR("Http error(%d)", code);
    handle_func_("error", method_, custom_data_, EV_HTTP_ERROR_CODE);
    return ZISYNC_ERROR_HTTP_RETURN_ERROR;
  }

  std::string str_length = evhttp_find_header(input_headers, "Content-Length");
  int32_t int_length = std::atoi(str_length.c_str());
  std::string content_base64;
  content_base64.resize(int_length + 1);
  int32_t nread = evbuffer_remove(
      input_buffer, &(*content_base64.begin()), int_length);
  if (nread != int_length) {
    ZSLOG_ERROR("Get verify response content fail.");
    handle_func_("error", method_, custom_data_, EV_HTTP_ERROR_CONTENT);
    return ZISYNC_ERROR_HTTP_RETURN_ERROR;
  }
  std::string content = base64_decode(content_base64);
  handle_func_(method_, content, custom_data_, EV_HTTP_OK);

  struct evhttp_connection *evcon = evhttp_request_get_connection(req);
  if (evcon != NULL) {
    evhttp_connection_free(evcon);
  }

  return ZISYNC_SUCCESS;
}
  
  void HttpRequest::HandleTimeOut() {
	  HandleRequest(NULL);
      assert(req_);
      struct evhttp_connection *conn = evhttp_request_get_connection(req_);
      if (conn) {
        evhttp_cancel_request(req_);
        evhttp_connection_free(conn);
      }
  }
  
} // namespace zs
