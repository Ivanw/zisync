// // Copyright 2015, zisync.com
// 
#include <memory>
#include <cassert>
#include <vector>
#include <string>
#include <UnitTest++/UnitTest++.h>  // NOLINT
#include <UnitTest++/TestReporterStdout.h>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/database/content.h"
#include "zisync/kernel/utils/plain_config.h"

using zs::err_t;
using zs::IZiSyncKernel;
using zs::ZISYNC_SUCCESS;
using zs::ZISYNC_ERROR_SYNC_NOENT;
using zs::ZISYNC_ERROR_DIR_NOENT;
using zs::ZISYNC_ERROR_NOT_STARTUP;
using zs::ZISYNC_ERROR_INVALID_PATH;
using std::unique_ptr;
using zs::Config;
using zs::DefaultLogger;
using zs::ZSLOG_ERROR;
using std::string;
using zs::StringFormat;
using zs::OsGetFullPath;
using zs::PlainConfig;

static IZiSyncKernel *kernel;
static DefaultLogger logger("./Log");
std::string app_path;
std::string backup_root;
std::string test_root;

static int64_t last_time = -1;

static inline void PrintTime(const char *prefix) {
  if (last_time != -1 && prefix != NULL) {
    ZSLOG_ERROR("%s : %" PRId64, prefix, zs::OsTimeInMs() - last_time);
  }
  last_time = zs::OsTimeInMs();
}

struct SomeFixture {
  SomeFixture() {
    err_t zisync_ret;

    int ret = zs::OsDeleteDirectories(test_root);
    assert(ret == 0);
    ret = zs::OsDeleteDirectories(backup_root);
    assert(ret == 0);
    PrintTime(NULL);
    ret = zs::OsCreateDirectory(test_root, true);
    assert(ret == 0);
    ret = zs::OsCreateDirectory(backup_root, true);
    assert(ret == 0);
    zisync_ret = kernel->Initialize(
        app_path.c_str(), "test_kernel", "test", backup_root.c_str());
    assert(zisync_ret == ZISYNC_SUCCESS);
    zisync_ret = kernel->Startup(app_path.c_str(), 8848, NULL);
    assert(zisync_ret == ZISYNC_SUCCESS);
    PrintTime("Prepare");
  }
  ~SomeFixture() {
    PrintTime("OnTest");
    kernel->Shutdown();
    PrintTime("Shutdown");
  }
};

void SingleTest() {
  std::string db_root;
  assert(OsGetFullPath(".", &db_root) == 0);
  Config::SetHomeDir(db_root.c_str());
  assert(zs::OsPathAppend(&db_root, "Database") == 0);
  std::vector<std::string> tokens;
  tokens.push_back(std::string("anything"));
  PlainConfig::Initialize(db_root.c_str(), tokens);
  if (!zs::OsFileExists(db_root.c_str())) {
    zs::OsCreateDirectory(db_root, true);
  }
  const std::string test_key = "test-key";
  const std::string test_value = "test-value";
  assert(PlainConfig::SetValueForKey(test_key, test_value) == ZISYNC_SUCCESS);
  std::string value_ret;
  assert(PlainConfig::GetValueForKey(test_key, &value_ret) == ZISYNC_SUCCESS);

}

TEST(dictionary_access_with_no_kernel) {
  SingleTest();
}

class Predicate
{
 public:

  Predicate(const char *prefix)
      : prefix_(prefix), prefix_len_(strlen(prefix)) {}

  bool operator()(UnitTest::Test *test) const
  {
    return strncmp(test->m_details.testName, prefix_, prefix_len_) == 0;
  }
 private:
  const char *prefix_;
  size_t prefix_len_;
};

int main(int argc, char** argv) {
  logger.Initialize();
  // logger.error_to_stderr = true;
  // logger.info_to_stdout = true;
  int ret = OsGetFullPath(".", &app_path);
  assert(ret == 0);
  backup_root = app_path + "/Backup";
  test_root = app_path + "/Test";
    
  LogInitialize(&logger);
  kernel = zs::GetZiSyncKernel("actual");
  if (argc == 1) {
    UnitTest::RunAllTests();
  } else if (argc == 2) {
    char *prefix = argv[1];
    Predicate predicate(prefix);
    UnitTest::TestReporterStdout reporter;
    UnitTest::TestRunner runner(reporter);
    runner.RunTestsIf(
        UnitTest::Test::GetTestList(), NULL, predicate, 0);
  } else {
    assert(false);
  }
  logger.CleanUp();
  return 0;
}
// #endif 
