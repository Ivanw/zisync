// Copyright 2015 zisync.com

#include <memory>
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <UnitTest++/UnitTest++.h>  // NOLINT
#include <UnitTest++/TestReporterStdout.h>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/history/history.h"
#include "zisync/kernel/history/history_data_source.h"
#include "zisync/kernel/history/history_manager.h"


using zs::err_t;
using zs::ZISYNC_SUCCESS;
using zs::ZISYNC_ERROR_ADDRINUSE;
using zs::ZISYNC_ERROR_INVALID_PORT;
using zs::ZISYNC_ERROR_SYNC_NOENT;
using zs::ZISYNC_ERROR_SYNC_EXIST;
using zs::ZISYNC_ERROR_TREE_NOENT;
using zs::ZISYNC_ERROR_TREE_EXIST;
using zs::ZISYNC_ERROR_DIR_NOENT;
using zs::ZISYNC_ERROR_NOT_STARTUP;
using zs::ZISYNC_ERROR_NESTED_TREE;
using zs::ZISYNC_ERROR_INVALID_PATH;
using std::unique_ptr;
using zs::Config;
using zs::DefaultLogger;
using std::string;
using zs::StringFormat;
using zs::OsGetFullPath;
using namespace zs;
static IZiSyncKernel *kernel;
static DefaultLogger logger("./Log");
std::string app_path;
std::string backup_root;
std::string test_root;

string Devs[] = {"dev1", "dev2", "dev3", "dev4"};
int32_t Treeids[] = {1, 2, 3, 4};
int64_t Ts[] = {1111111, 2222222, 3333333, 4444444};
string froms[] = {"from1", "from2", "from3", "fro4"};
string to[] = {"to1", "to2", "to3", "to4"};
int codes[] = {FILE_OPERATION_CODE_ADD, FILE_OPERATION_CODE_DELETE,
               FILE_OPERATION_CODE_MODIFY, FILE_OPERATION_CODE_ADD};
int errors[] = {FILE_OPERATION_ERROR_NONE, FILE_OPERATION_ERROR_CONFLICT,
                FILE_OPERATION_ERROR_MOD_RDONLY, FILE_OPERATION_ERROR_NONE};
int HistLen = sizeof(Ts) / sizeof(Ts[0]);

static int64_t last_time = -1;
static inline void PrintTime(const char *prefix) {
  if (last_time != -1 && prefix != NULL) {
    ZSLOG_ERROR("%s : %" PRId64, prefix, zs::OsTimeInMs() - last_time);
  }
  last_time = zs::OsTimeInMs();
}

class TestDataSource: public IHistoryDataSource {
  public:
  virtual ~TestDataSource(){}
  TestDataSource();

  err_t GetHistoryItems(vector<History> *histories, int limit = -1);
  err_t StoreHistoryItems(vector<unique_ptr<History> > *histories){return ZISYNC_SUCCESS;}
  err_t Initialize(ILibEventBase *base){return ZISYNC_SUCCESS;}
  err_t CleanUp(){return ZISYNC_SUCCESS;}
  private:
  vector<unique_ptr<History> > histories_;
};

TestDataSource::TestDataSource() {
  for (int i = 0; i < HistLen; i++) {
    histories_.emplace_back(CreateHistory(Devs[i], Treeids[i], 0,
                                          froms[i], to[i].c_str(),
                                          Ts[i], codes[i], errors[i]));
  }
}

err_t TestDataSource::GetHistoryItems(vector<History> *histories, int limit) {
  vector<unique_ptr<History> >::const_iterator beg = histories_.begin();
  if (limit != 0 && (unsigned)limit < histories_.size()) {
    beg += histories_.size() - unsigned(limit);
  }
  for (; beg != histories_.end(); ++beg) {
    histories->emplace_back(**beg);
  }
  return ZISYNC_SUCCESS;
}

//TEST(dummydata) {
//  ILibEventBase *base = GetEventBase();
//  IHistoryManager *hm = GetHistoryManager();
//  IHistoryDataSource *data_source = new TestDataSource();
//  hm->Initialize(unique_ptr<IHistoryDataSource>(data_source));
//  base->RegisterVirtualServer(hm);
//  base->Startup();
//
//  for(int i = 0; i < HistLen; i++) {
//    hm->AppendHistory(Devs[i], Treeids[i], Ts[i],
//                      froms[i], errors[i], codes[i],
//                      to[i].c_str());
//  }
//  vector<History> query;
//  hm->QueryHistories(&query, -1);
//  for(unsigned i = 0; i < query.size(); i++) {
//    Check(query[i], i);
//  }
//
//  base->Shutdown();
//}
//
struct SomeFixture {
  SomeFixture() {
    err_t zisync_ret;

    PrintTime(NULL);
    int ret = OsGetFullPath(".", &app_path);
    assert(ret == 0);
    backup_root = app_path + "/Backup";
    test_root = app_path + "/Test";
    zs::OsCreateDirectory(test_root, false);
    zs::OsCreateDirectory(backup_root, false);

    LogInitialize(&logger);
    kernel = zs::GetZiSyncKernel("actual");
    zisync_ret = kernel->Initialize(
      app_path.c_str(), "zisyncsw", "zisyncsw", backup_root.c_str());
    assert(zisync_ret == ZISYNC_SUCCESS);
    zisync_ret = kernel->Startup(app_path.c_str(), 8848, NULL);
    assert(zisync_ret == ZISYNC_SUCCESS);
    PrintTime("Prepare");
  }
  ~SomeFixture() {
    PrintTime("OnTest");
    kernel->Shutdown();
    PrintTime("Shutdown");
    int ret = zs::OsDeleteDirectories(test_root);
    assert(ret == 0);
    ret = zs::OsDeleteDirectories(backup_root);
    assert(ret == 0);   
  }
};

class TestDataSource2: public HistoryDataSource {
  public:
  virtual ~TestDataSource2(){reloaded_sig_.CleanUp();}
  TestDataSource2():HistoryDataSource(){
    has_initialized_ = false;
    reloaded_sig_.Initialize(false);}
  OsEvent reloaded_sig_;
  void ReloadDataSource() {
    HistoryDataSource::ReloadDataSource(1000);
    if (has_initialized_ && GetCnt() == (unsigned)HistLen) {
      reloaded_sig_.Signal();
    }else {
      has_initialized_ = true;
    }
  }
  private:
  bool has_initialized_;
};

#ifdef ZS_TEST

static void Check(const History &item, unsigned i) {
  assert(item.modifier == Devs[i]);
  assert(item.time_stamp == Ts[i]);
  assert(item.frompath == froms[i]);
  if (item.topath != "") {
    assert(item.topath == to[i]);
  }
  assert(item.code == codes[i]);
  assert(item.error == errors[i]);
}

TEST_FIXTURE(SomeFixture, database) {
  IHistoryManager *hm = GetHistoryManager();
  IHistoryDataSource *data_source = new TestDataSource2();
  hm->ChangeDataSource(data_source);

  for(int i = 0; i < HistLen; i++) {
    hm->AppendHistory(Devs[i], Treeids[i], 0, Ts[i],
                      froms[i], errors[i], codes[i],
                      to[i].c_str());
  }
  ((TestDataSource2*)data_source)->reloaded_sig_.Wait();
  QueryHistoryResult queryRes;
  kernel->QueryHistoryInfo(&queryRes);
  vector<History> &query = queryRes.histories;
  for(unsigned i = 0; i < query.size(); i++) {
    Check(query[i], i);
  }

}
#endif

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
