// Copyright 2014, zisync.com
#include <cassert>
#include <unistd.h>
#include <signal.h>

#include "zisync_kernel.h"

#include "zisync/kernel/platform/platform.h"

using zs::IZiSyncKernel;
using zs::err_t;
using zs::ZISYNC_SUCCESS;

static void sig_exit(int signo) {
}

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Invalid argc : %d", argc);
    return 1;
  }
  zs::DefaultLogger logger("./daemon/Log");
  logger.warning_to_stdout = true;
  logger.error_to_stderr = true;
  logger.info_to_stdout = true;
  logger.Initialize();
  zs::LogInitialize(&logger);
  IZiSyncKernel* kernel = zs::GetZiSyncKernel("actual");
  err_t ret;
  std::string pwd;
  zs::OsGetFullPath(".", &pwd);
  zs::OsDeleteDirectories(pwd + "/daemon/backup");
  zs::OsCreateDirectory(pwd + "/daemon/backup", true);
  ret = kernel->Startup((pwd + "/daemon").c_str(), 8848, NULL);
  if (ret == zs::ZISYNC_ERROR_CONFIG) {
    ret = kernel->Initialize(
        (pwd + "/daemon").c_str(), argv[1], "test", 
        (pwd + "/daemon/backup").c_str());
    assert(ret == ZISYNC_SUCCESS);
    ret = kernel->Startup((pwd + "/daemon").c_str(), 8848, NULL);
  } 
  assert(ret == ZISYNC_SUCCESS);
  signal(SIGINT, sig_exit);
  pause();
  kernel->Shutdown();

  return 0;
}
