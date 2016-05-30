// Copyright 2014, zisync.com
//#include <glog/logging.h>
#include <string>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/database/icore.h"

namespace zs {

DefaultLogger::DefaultLogger(const char* log_dir):
    log_dir_(log_dir) , info_to_stdout(false), warning_to_stdout(false),
	error_to_stderr(false), fatal_to_stderr(false){}
DefaultLogger::~DefaultLogger() {
}

void DefaultLogger::Initialize() {
  /*FLAGS_log_dir = log_dir_;
  FLAGS_max_log_size = 100;
  FLAGS_logbufsecs = 0;
  google::SetLogFilenameExtension("log");
  google::InitGoogleLogging("ZiSyncKernel");*/
}

void DefaultLogger::CleanUp() {
  //google::ShutdownGoogleLogging();
}


void DefaultLogger::AppendToLog(
    int severity,
    const char* full_filename, int line,
    const char* log_time, int thread_id,
    const char* message, size_t message_len) {
  /*google::LogSeverity glog_severity;
  switch (severity) {
    case ZSLOG_INFO:
      glog_severity = google::GLOG_INFO;
      break;
    case ZSLOG_WARNING:
      glog_severity = google::GLOG_WARNING;
      break;
    case ZSLOG_ERROR:
      glog_severity = google::GLOG_ERROR;
      break;
    case ZSLOG_FATAL:
      glog_severity = google::GLOG_FATAL;
      break;
  }
  google::LogMessage(full_filename, line, glog_severity).stream() << message;*/
}

}  // namespace zs
