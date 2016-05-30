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

#ifndef ZISYNC_KERNEL_LIBEVENT_REPORT_DATA_SERVER_H_
#define ZISYNC_KERNEL_LIBEVENT_REPORT_DATA_SERVER_H_

#include <openssl/ssl.h>
#include <memory>
#include "zisync/kernel/libevent/libevent++.h"
#include "zisync/kernel/proto/verify.pb.h"

namespace zs {

class IReportDataSource;

class ReportDataServer : public ILibEventVirtualServer
                       , public ITimerEventDelegate {
 public:
  ReportDataServer();
  virtual ~ReportDataServer();

  err_t Initialize(std::shared_ptr<IReportDataSource> data_source);
  void CleanUp();
  
  //
  // Implement ILibEventVirtualServer
  //
  virtual err_t Startup(ILibEventBase* base);
  virtual err_t Shutdown(ILibEventBase* base);

  //
  // Implement  ITimerEventDelegate
  //
  virtual void OnTimer(TimerEvent* timer_event);

  //
  // Implment IBufferEventDelegate
  //
  void SendRequest(const std::string &scheme,
                   IHttpRequest* request);
  void Verify(const std::string &mactoken, const std::string &keycode);
                         void Bind(const std::string &mactoken, const std::string &keycode, const std::string &data);
                         void Unbind(const std::string &mactoken, const std::string &keycode, const std::string &data);
  void Feedback(const std::string &type, const std::string &version,
                const std::string &message, const std::string &contact, const std::string &data);
  void ReportMactoken(const std::string &mactoken);
  void ReportData(const std::string &type);

 public:
  static ReportDataServer* GetInstance() {return &s_instance;}

                         IHttpRequest *CreateVerifyRequest(const std::string &mactoken, const std::string &keycode, const std::string &data = std::string());
                         
 void DelayAndBind();

 bool WillBind() {return will_bind_;}
 void SetWillBind(bool will) {will_bind_ = will;}
                         
 protected:
  static ReportDataServer s_instance;

 protected:
  TimerEvent* timer_event_;
  std::shared_ptr<IReportDataSource> data_source_;
  ILibEventBase *base_;

  SSL_CTX *ssl_ctx_;
  X509 *cert_;
  X509_STORE *x509_store_;
                         
  bool will_bind_;
};

class IReportDataSource {
 public:
  virtual ~IReportDataSource() {}
  virtual err_t GetVerifyData(const char* report_type, std::string* buffer) = 0;
};

class StatisticDataSource : public IReportDataSource {
 public:
  virtual ~StatisticDataSource() {}
  virtual err_t GetVerifyData(const char* report_type, std::string* buffer);
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_LIBEVENT_REPORT_DATA_SERVER_H_
