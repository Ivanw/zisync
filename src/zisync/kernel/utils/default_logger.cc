// Copyright 2014, zisync.com
#include <string>

#include "zisync/kernel/platform/platform.h"
#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/database/icore.h"


namespace zs {

DefaultLogger::DefaultLogger(const char* log_dir):
    info_to_stdout(false), warning_to_stdout(false), 
    error_to_stderr(false), fatal_to_stderr(false),
    log_dir_(log_dir), log_fp_(NULL)  {
}
DefaultLogger::~DefaultLogger() {
}

void DefaultLogger::Initialize() {
  string log_file = log_dir_;
  int ret = zs::OsCreateDirectory(log_dir_, false);
  assert(ret == 0);
  OsPathAppend(&log_file, std::string("ZiSyncKernel.log"));
  log_fp_ = fopen(log_file.c_str(), "a");
  if (log_fp_ == NULL) {
    fprintf(stderr, "failed to fopen log file: %s\n", log_dir_.c_str());
    assert(false);
  }
}

void DefaultLogger::CleanUp() {
  fclose(log_fp_);
}

void DefaultLogger::AppendToLog(
    int severity,
    const char* full_filename, int line,
    const char* log_time, int thread_id,
    const char* message, size_t message_len) {
   fseek(log_fp_, 0, SEEK_END);
   string buf;
   StringFormat(&buf, "%s %s:%d] [%x] %s", log_time, full_filename, line,
                thread_id, message);
  switch (severity) {

    case ZSLOG_INFO:
      fprintf(log_fp_, "I%s\n", buf.c_str());
      if (info_to_stdout) {
        fprintf(stdout, "I%s\n", buf.c_str());
      }
      break;
    case ZSLOG_WARNING:
      fprintf(log_fp_, "W%s\n", buf.c_str());
      if (warning_to_stdout) {
        fprintf(stdout, "W%s\n", buf.c_str());
      }
      break;
    case ZSLOG_ERROR:
      fprintf(log_fp_, "E%s\n", buf.c_str());
      if (error_to_stderr) {
        fprintf(stdout, "E%s\n", buf.c_str());
      }
      break;
    case ZSLOG_FATAL:
      fprintf(log_fp_, "F%s\n", buf.c_str());
      if (fatal_to_stderr) {
        fprintf(stdout, "F%s\n", buf.c_str());
      }
      break;
  }
}

}  // namespace zs
