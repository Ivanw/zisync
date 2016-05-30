// Copyright 2014, zisync.com
// 
#include <memory>
#include <cassert>
#include <iostream>
#include <UnitTest++/UnitTest++.h>  // NOLINT
#include <UnitTest++/TestReporterStdout.h>
#include <thread>         // std::this_thread::sleep_for
#include <chrono>         // std::chrono::seconds
#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/tree_status.h"

using std::cout;
using std::endl;
using zs::err_t;
using zs::IZiSyncKernel;
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
using zs::SyncInfo;
using zs::BackupInfo;
using zs::TreeInfo;
using zs::Config;
using zs::OsTcpSocket;
using zs::OsUdpSocket;
using zs::DefaultLogger;
using zs::FindRequest;
using zs::MsgStat;
using zs::ZSLOG_ERROR;
using std::string;
using std::vector;
using zs::StringFormat;
using zs::OsGetFullPath;
using zs::TransferList;
using namespace zs;

static DefaultLogger logger("./Log");
std::string test_root;
const int MaxTransfer = 3;
const int MaxFilePerTask = 5;
const int defaultFileSize = 10;
static int last_time = -1;
static TransferList transferList;
static OsMutex mut_;

struct Task {
    bool has_been_added;
    bool is_up_load;
    int32_t remote_tree_id;
    vector<string> files;
    vector<int> sizes;
    Task():has_been_added(false), is_up_load(false){}
};
struct Transfer {
    Task up;
    Task down;
};

static vector<Transfer> transfers(MaxTransfer, Transfer());
static inline void PrintTime(const char *prefix) {
    if (last_time != -1 && prefix != NULL) {
        ZSLOG_ERROR("%s : %" PRId64, prefix, zs::OsTimeInMs() - last_time);
    }
    last_time = zs::OsTimeInMs();
}

void makeTask(Task *task_, bool is_up_load, int transfer_id) {
    char buf[1000];
    task_->remote_tree_id = transfer_id;
    task_->is_up_load = is_up_load;
    for (int i = 0; i < MaxFilePerTask; i++) {
        sprintf(buf, "task(%d), %s, file(%d)", transfer_id, is_up_load ? "upload" : "download", (int)i+1);
        task_->files.push_back(string(buf));
        task_->sizes.push_back(defaultFileSize);
    }
}

void makeTasks() {
    for (int i = 0; i < MaxTransfer; i++) {
        makeTask(&transfers[i].up, true, i);
        makeTask(&transfers[i].down, false, i);
    } 
}

#define ASSERT(err) CHECK_EQUAL(err, ZISYNC_SUCCESS)

void AppendFiles(const vector<string> &files, int32_t remote_tree_id) {
  for(auto it = files.begin(); it != files.begin(); ++it) {
        transferList.AppendFile(*it, *it+"remote", std::string(), defaultFileSize, remote_tree_id);        
    }
}

void AppendFiles(Task &tsk, int32_t remote_tree_id) {
    if(!tsk.has_been_added) {
        tsk.has_been_added = true;
        transferList.OnTransferListBegin(tsk.is_up_load? ST_PUT: ST_GET, tsk.remote_tree_id);      
        AppendFiles(tsk.files, remote_tree_id);
    }
}

bool executeTask(Task &task_) {
    int dec = 0;
    for(int j = 0; j < MaxFilePerTask; j++) {
        MutexAuto mut(&mut_);
        int &remain = task_.sizes[j];
        if(remain > 0) {
            if(remain == defaultFileSize) {
                AppendFiles(task_, task_.remote_tree_id);
            }
            while(dec == 0) {
                dec = rand() % defaultFileSize;
                if(dec > remain) {
                    dec = remain;
                }
            }
            transferList.OnByteTransfered(task_.is_up_load ? ST_PUT: ST_GET, dec, task_.remote_tree_id, std::string());        
            remain -= dec;
            if(remain <= 0) {             
                transferList.OnFileTransfered(task_.is_up_load ? ST_PUT: ST_GET, task_.remote_tree_id, std::string());  
                if(j == MaxFilePerTask - 1) {
                    transferList.OnTransferListEnd(task_.is_up_load ? ST_PUT: ST_GET, task_.remote_tree_id);
                }
            }
            return true;
        }

    }
    return false;
}

class DummyTransfer: public OsThread {
    public:
    DummyTransfer():OsThread("DummyTransfer"){}
    void SetId(int i){id_ = i;}
    virtual int Run() {
        while(true) {
            if(!executeTask(transfers[id_].up)){
                if(!executeTask(transfers[id_].down)){
                    break;
                }
            }
            cout << "one item" << endl;
            std::chrono::milliseconds dura( rand() % 1000 );
            std::this_thread::sleep_for( dura  );
        }
        return 0;
    }
    private:
    int id_;
};

DummyTransfer dummyTransfer[MaxTransfer];

class DummyQuery: public OsThread {
    public:
    DummyQuery():OsThread("DummyQuery"){}
    int Run() {
        TransferListStatus tlist;
        int cnt = 1;
        while(true) {
            //MutexAuto mut(&mut_);
            transferList.QueryTransferList(&tlist, 0, 20);
            auto it = tlist.list_.begin();
            cout << "Queryed Results(" << tlist.list_.size() << "):"<< " trial: " << cnt++ << endl;
            for(; it != tlist.list_.end(); ++it) {
                cout << it ->local_path << " --> " << it ->remote_path << " " << it ->bytes_to_transfer << "/" << it ->bytes_file_size << endl;
            }
            std::chrono::milliseconds dura( rand() % 1000  );
            std::this_thread::sleep_for( dura  );
        }
        return 0;
    }

    int Shutdown() {
        for(int i = 0; i < MaxTransfer; i++) {
            dummyTransfer[i].Shutdown();
        }
        return 0;
    }
};
DummyQuery query;

void ShowTransferTasks() {
    for(int i = 0; i < MaxTransfer; i++) {
        for(int j = 0; j < MaxFilePerTask; j++) {
            cout << transfers[i].up.files[j] << endl;
        }
        for(int j = 0; j < MaxFilePerTask; j++) {
            cout << transfers[i].down.files[j] << endl;
        }
    }
}

struct SomeFixture {
    SomeFixture() {
        PrintTime(NULL);
        PrintTime("Prepare");
        makeTasks();
        ShowTransferTasks();
    }
    ~SomeFixture() {
        PrintTime("OnTest");
        PrintTime("Shutdown");
    }
};

TEST_FIXTURE (SomeFixture, test) {
    for(int i = 0; i < MaxTransfer; i++) {
      dummyTransfer[i].SetId(i);
      dummyTransfer[i].Startup();
    }
    query.Startup();
    cout << "waiting .." << endl;
    query.Shutdown();
}

// #ifndef _MSC_VER

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
    int prefix_len_;
};


int main(int argc, char** argv) {
    logger.Initialize();
    LogInitialize(&logger);
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
