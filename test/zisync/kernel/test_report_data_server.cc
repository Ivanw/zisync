/**
 * @file test_database.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief main() for database related testing.
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
#include <unistd.h>
#include <memory>
#include <cassert>
#include <cstring>
#include <UnitTest++/UnitTest++.h>
#include <UnitTest++/TestReporterStdout.h>
#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/libevent/report_data_server.h"

using zs::IZiSyncKernel;
using zs::err_t;

namespace zs {

class TestStatisticDataSource : public IReportDataSource {
 public:
  virtual ~TestStatisticDataSource() {}
  virtual err_t GetVerifyData(const char* report_type, std::string* buffer) {
    return ZISYNC_SUCCESS;
  }
};

TEST(ReportDataServer) {
  IReportDataSource* data_source = new TestStatisticDataSource;
  std::shared_ptr<IReportDataSource> source(data_source);
  ReportDataServer* data_server = new ReportDataServer;
  data_server->Initialize(source);

  ILibEventBase* event_base = GetEventBase();
  event_base->RegisterVirtualServer(ReportDataServer::GetInstance());
  event_base->Startup(); 

  sleep(1000);

  event_base->UnregisterVirtualServer(ReportDataServer::GetInstance());
  event_base->Shutdown(); 
  
  if (data_server) {
    delete data_server;
    data_server = NULL;
  }
}

}

int main(int argc, char** argv ) {
  zs::DefaultLogger logger("./Log");
  logger.Initialize();
  logger.error_to_stderr = true;
  LogInitialize(&logger);
  UnitTest::RunAllTests();
  logger.CleanUp();
  return 0;
}
