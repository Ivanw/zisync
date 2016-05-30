// Copyright 2014, zisync.com

#include <cassert>
#include <memory>
#include <algorithm>

#include "zisync/kernel/worker/refresh_worker.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/vector_clock.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/utils/usn.h"
#include "zisync/kernel/utils/read_fs_task.h"
#include "zisync/kernel/utils/inner_request.h"
#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/transfer/task_monitor.h"
#include "zisync/kernel/utils/event_notifier.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/monitor/monitor.h"
#include "zisync/kernel/monitor/fs_monitor.h"
#include <iostream>
namespace zs {

using std::unique_ptr;
using std::vector;

class RefreshHandler : public MessageHandler {
 public:
  virtual ~RefreshHandler() {}
  virtual ::google::protobuf::Message* mutable_msg() {
    return &request_msg_;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);

  void HandlerInern(int32_t tree_id);

 protected:
  MsgRefreshRequest request_msg_;
};

RefreshWorker::RefreshWorker()
    : Worker("RefreshWorker") {
  msg_handlers_[MC_REFRESH_REQUEST] = new RefreshHandler;
}

err_t RefreshWorker::Initialize() {
  err_t zisync_ret;
  assert(req != NULL);
  assert(exit != NULL);
  zisync_ret = req->Connect(router_refresh_backend_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = exit->Connect(exit_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  return ZISYNC_SUCCESS;
}

class RefreshTask : public ReadFsTask {
 public:
  RefreshTask(
      const char *tree_uuid_, const char *tree_root, int32_t tree_id,
      int32_t type):
      ReadFsTask(tree_uuid_, tree_root, tree_id, type), 
      visitor(tree_root, tree_id),
      traverser(tree_root, &visitor), tree_id(tree_id) {}

  virtual err_t Run();

  FsVisitor visitor;
  OsFsTraverser traverser;

 private:
  RefreshTask(RefreshTask&);
  void operator=(RefreshTask&);

  int32_t tree_id;
};

err_t RefreshTask::Run() {
  IContentResolver *resolver = GetContentResolver();
  traverser.traverse();
  visitor.sort();
  IndexMonitor monitor(tree_id, visitor.files()->size());

  unique_ptr<ICursor2> cursor(resolver->sQuery(
          TableFile::GenUri(tree_uuid_.c_str()), 
          FileStat::file_projs_without_remote_vclock, 
          FileStat::file_projs_without_remote_vclock_len, 
          NULL, TableFile::COLUMN_PATH));

  auto fs_iter = visitor.files()->begin();
  auto fs_end = visitor.files()->end();
  unique_ptr<FileStat> file_in_db;
  if (cursor->MoveToNext()) {
    file_in_db.reset(new FileStat(cursor.get()));
  }
  while ((!cursor->IsAfterLast()) || (fs_iter != fs_end)) {
    err_t add_op_ret = ZISYNC_SUCCESS;
    bool inc_fs = false;
    bool inc_db = false;
    string temp_path;
    if (fs_iter != fs_end) {
      temp_path = (*fs_iter)->path();
    }
    if (zs::IsAborted() || zs::AbortHasFsTreeDelete(tree_id)) {
      return ZISYNC_SUCCESS;
    }
    if (fs_iter == fs_end) {
      AddRemoveFileOp(std::move(file_in_db));
      inc_db = true;
    } else if (cursor->IsAfterLast()) {
      (*fs_iter)->time_stamp = OsTimeInS();
      monitor.OnFileWillIndex((*fs_iter)->path());
      add_op_ret = AddInsertFileOp(unique_ptr<FileStat>(fs_iter->release()));
      inc_fs = true;
    } else {
      int ret = strcmp((*fs_iter)->path(), file_in_db->path());
      if (ret == 0) {
        /*  @TODO push int update */
        monitor.OnFileWillIndex((*fs_iter)->path());
        add_op_ret = AddUpdateFileOp(std::move(*fs_iter), std::move(file_in_db));
        inc_db = true;
        inc_fs = true;
      } else if (ret < 0) {
        monitor.OnFileWillIndex((*fs_iter)->path());
        add_op_ret = AddInsertFileOp(unique_ptr<FileStat>(fs_iter->release()));
        // fs little, insert into db, get next one in fs
        inc_fs = true;
      } else {
        // db little, remove from db, get next one in db
        AddRemoveFileOp(std::move(file_in_db));
        inc_db = true;
      }
    }
    
    if (add_op_ret == ZISYNC_ERROR_SHA1_FAIL) {
      FsEvent evt;
      evt.path = tree_root_ + temp_path;
      evt.type = FS_EVENT_TYPE_MODIFY;
      evt.file_move_cookie = 0;
      Monitor::GetMonitor()->ReportFailBack(tree_root_, evt);
    }
    
    if (inc_fs) {
      fs_iter ++;
      monitor.OnFileIndexed(1);
    }
    if (inc_db && cursor->MoveToNext()) {
      file_in_db.reset(new FileStat(cursor.get()));
    }
  }
  cursor.reset(NULL);

  ApplyBatchTail();
  return error;
}

void RefreshHandler::HandlerInern(int32_t tree_id) {
  unique_ptr<Tree> tree(Tree::GetBy(
        "%s = %" PRId32 " AND %s = %d AND %s = %d", 
        TableTree::COLUMN_ID, tree_id,
        TableTree::COLUMN_STATUS, TableTree::STATUS_NORMAL,
        TableTree::COLUMN_DEVICE_ID, TableDevice::LOCAL_DEVICE_ID));

  if (tree) {
    ZSLOG_INFO("Start RefreshTask(%s)", tree->root().c_str());
    int32_t type = ReadFsTask::GetType(*tree);
    if (type == -1) {
      ZSLOG_ERROR("ReadFsTask::GetType() fail");
      return;
    }

    bool root_exist = OsDirExists(tree->root());
    if (root_exist && tree->root_status() == TableTree::ROOT_STATUS_REMOVED) {
      //restore normal refresh
      ContentValues cv(2);
      cv.Put(TableTree::COLUMN_IS_ENABLED, true);
      cv.Put(TableTree::COLUMN_ROOT_STATUS, TableTree::ROOT_STATUS_NORMAL);
      GetContentResolver()->Update(TableTree::URI, &cv, "%s = %" PRId32,
          TableTree::COLUMN_ID, tree->id());
      tree->ResumeTreeWatch();
      ZSLOG_INFO("Tree(%s) root(%s) restored."
          , tree->uuid().c_str(), tree->root().c_str());

    }else if (!root_exist && tree->root_status() == TableTree::ROOT_STATUS_NORMAL){
      //set root_moved
      //make tree disabled
      assert(false);
      ContentValues cv(2);
      cv.Put(TableTree::COLUMN_ROOT_STATUS, TableTree::ROOT_STATUS_REMOVED);
      cv.Put(TableTree::COLUMN_IS_ENABLED, false);
      GetContentResolver()->Update(TableTree::URI, &cv, "%s = %" PRId32,
          TableTree::COLUMN_ID, tree->id());
      ZSLOG_INFO("Tree(%s) root(%s) removed."
          , tree->uuid().c_str(), tree->root().c_str());
      return;
    }else if (!root_exist) {
      ZSLOG_INFO("Tree(%s) root(%s) being ignored."
          , tree->uuid().c_str(), tree->root().c_str());
      return;
    }

    RefreshTask refresh_task(
        tree->uuid().c_str(), tree->root().c_str(), 
        request_msg_.tree_id(), type);
    refresh_task.set_backup_type(tree->type());

    unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(tree->sync_id()));
    assert(sync);
    zs::GetEventNotifier()->NotifyIndexStart(tree->sync_id(), ToExternalSyncType(sync->type()));
    refresh_task.Run();
    /*  TODO if Fail */
    zs::GetEventNotifier()->NotifyIndexFinish(tree->sync_id(), ToExternalSyncType(sync->type()));
    ZSLOG_INFO("End RefreshTask(%s)", tree->root().c_str());
  }
  return;
}

err_t RefreshHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
#ifdef ZS_TEST
  if (Config::is_refresh_enabled()) {
    HandlerInern(request_msg_.tree_id());
  }
#else
  HandlerInern(request_msg_.tree_id());
#endif

  RefreshResponse response;
  response.mutable_response()->set_tree_id(request_msg_.tree_id());
  err_t zisync_ret = response.SendTo(socket);
  assert(zisync_ret == ZISYNC_SUCCESS);

  return ZISYNC_SUCCESS;
}

}  // namespace zs
