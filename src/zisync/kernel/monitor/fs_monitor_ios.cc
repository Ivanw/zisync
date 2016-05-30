// Copyright 2014, zisync.com
#include <string>
#include <errno.h>
#include <zmq.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "zisync_kernel.h"
#include "zisync/kernel/monitor/fs_monitor.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/message.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/monitor/MonitorReportEventRequestIOS.h"
#include "zisync/kernel/utils/context.h"

namespace zs {
using std::string;
class ZmqContext;

class FsMonitor : public IFsMonitor, public OsThread {
 private:
  IFsReporter *reporter_;
  ZmqSocket *req_, *exit_sub_;

  FsMonitor(FsMonitor&);
  void operator=(FsMonitor&);

 public:
  FsMonitor():OsThread("FsMonitor"), reporter_(NULL), req_(NULL), exit_sub_(NULL) {} 
  ~FsMonitor(){
    if(req_ != NULL){
      delete req_;
      req_ = NULL;
    }
    if(exit_sub_ != NULL){
      delete exit_sub_;
      exit_sub_ = NULL;
    }
  }
  /*  when FsMonitor found a new dir or file created, modified or deleted, call 
   *  reporter.Report(). */
  virtual int Startup(IFsReporter* reporter);
  virtual int Shutdown();
  virtual int AddWatchDir(const std::string& dir_path);
  virtual int DelWatchDir(const std::string& dir_path);
  virtual int Run();

};

// Singleton
static FsMonitor fs_monitor;
const char fs_monitor_req_uri[] = "inproc://receive_ui_report";
IFsMonitor* GetFsMonitor() {
  return &fs_monitor;
}

const ZmqContext *shareContext(const ZmqContext *ctx) {
  static const ZmqContext *context_;
  if(ctx != NULL){
    context_ = ctx;
  }
  return context_;
}

int FsMonitor::Startup(IFsReporter* reporter) {
  reporter_ = reporter;
  const ZmqContext &context = GetGlobalContext();
  err_t zisync_ret;

  shareContext(&context);

  if(req_ == NULL){
    req_ = new ZmqSocket(context, ZMQ_ROUTER);
    zisync_ret = req_ ->Bind(fs_monitor_req_uri);
    assert( zisync_ret == ZISYNC_SUCCESS );
  }
  assert( req_ != NULL );

  if( exit_sub_ == NULL ){
    exit_sub_ = new ZmqSocket(context, ZMQ_SUB );
    assert( exit_sub_ != NULL );
    zisync_ret = exit_sub_ ->Connect(zs::exit_uri);
    assert( zisync_ret == ZISYNC_SUCCESS );
  }
  ZSLOG_INFO("starting up FsMonitor");
  return OsThread::Startup();
}

int FsMonitor::Run(){
  while(1){
    zmq_pollitem_t items[] = {
      {exit_sub_ ->socket(), -1, ZMQ_POLLIN, 0},
      {req_ ->socket(), -1, ZMQ_POLLIN, 0},
    };
    int ret = zmq_poll(items, sizeof(items) / sizeof(zmq_pollitem_t), -1);
    if(ret == -1){
      continue;
    }

    if( items[0].revents & ZMQ_POLLIN ){
      break;
    }
    if( items[1].revents & ZMQ_POLLIN ){
      err_t err;
      ZmqIdentify iden;
      err = iden.RecvFrom(*req_);
      if(err == ZISYNC_SUCCESS){
        MonitorReportEventRequestIOS request;
        err = request.RecvFrom(*req_);
        assert( err == ZISYNC_SUCCESS );
        MsgMonitorReportEventRequestIOS *request_msg = request.mutable_request();
        int nevents = request_msg ->events_size();
        if(nevents > 0){
          FsEvent fsevent;
          fsevent.path =  request_msg ->events(0).path();
          MsgReportEventTypeIOS type = request_msg ->events(0).type();
          if(type == ET_CREATE_IOS){
            fsevent.type = FS_EVENT_TYPE_CREATE;
            string dir = request_msg ->tree_root();
            reporter_ ->Report(dir, fsevent);
          }else if(type == ET_DELETE_IOS){
            fsevent.type = FS_EVENT_TYPE_DELETE;
            string dir = request_msg ->tree_root();
            reporter_ ->Report(dir, fsevent);
          }
        }
        err = iden.SendTo(*req_);
        if(err == ZISYNC_SUCCESS){
          err = request.SendTo(*req_);
          if(err != ZISYNC_SUCCESS){
            ZSLOG_INFO("response err");
          }
        }
      }
    }
  }
  return 0;
}
int FsMonitor::Shutdown() {
  // (wansong): implment it
  int ret = OsThread::Shutdown();
  if(ret != 0){
    return ret;
  }
  if(req_ != NULL){
    delete req_;
    req_ = NULL;
  }
  if(exit_sub_ != NULL){
    delete exit_sub_;
    exit_sub_ = NULL;
  }
  ZSLOG_INFO("FsMonitor Shutdown");
  return 0;
}
int FsMonitor::AddWatchDir(const std::string& dir_path) {
  ZSLOG_INFO("FsMonitor::AddWatchDir");
  // (wansong): implment it
  return 0;
}

int FsMonitor::DelWatchDir(const std::string& dir_path) {
  ZSLOG_INFO("FsMonitor::DelWatchDir");
  // (wansong): implment it
  return 0;

}

}  // namespace zs
