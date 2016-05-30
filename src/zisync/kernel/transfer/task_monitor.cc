/**
 * @file transfer_monitor.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief default transfer monitor implementation.
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

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/zslog.h"

#include <tuple>

#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/event_notifier.h"
#include "zisync/kernel/transfer/task_monitor.h"
#include "zisync/kernel/libevent/libevent++.h"
#include "zisync/kernel/utils/sync.h"

namespace zs {

using std::shared_ptr;
using std::weak_ptr;
using std::unique_ptr;

static void LambdaOnFileTransfered(void *ctx) {
  assert(ctx != NULL);
  auto bar = reinterpret_cast<std::tuple<
      weak_ptr<ITransferList>, StatusType, int32_t, std::string>*>(ctx);
  shared_ptr<ITransferList> stat = std::get<0>(*bar).lock();
  if (stat) {
    stat->OnFileTransfered(
        std::get<1>(*bar), std::get<2>(*bar), std::get<3>(*bar));
  }

  delete bar;
}

static void LambdaOnFileSkiped(void *ctx) {
  assert(ctx != NULL);
  auto bar = reinterpret_cast<std::tuple<
      weak_ptr<ITransferList>, StatusType, int32_t, std::string>*>(ctx);
  shared_ptr<ITransferList> stat = std::get<0>(*bar).lock();
  if (stat) {
    stat->OnFileSkiped(
        std::get<1>(*bar), std::get<2>(*bar), std::get<3>(*bar));
  }

  delete bar;
}

static void LambdaOnByteTransfered(void *ctx) {
  assert(ctx != NULL);
  auto bar = reinterpret_cast<std::tuple<weak_ptr<ITransferList>, StatusType,
       int64_t, int32_t, std::string>*>(ctx);
  shared_ptr<ITransferList> stat = std::get<0>(*bar).lock();
  if (stat) {
    stat->OnByteTransfered(std::get<1>(*bar), std::get<2>(*bar),
                           std::get<3>(*bar), std::get<4>(*bar));
  }

  delete bar;
}

static void LambdaOnByteSkiped(void *ctx) {
  assert(ctx != NULL);
  auto bar = reinterpret_cast<std::tuple<weak_ptr<ITransferList>, StatusType,
       int64_t, int32_t, std::string>*>(ctx);
  shared_ptr<ITransferList> stat = std::get<0>(*bar).lock();
  if (stat) {
    stat->OnByteSkiped(std::get<1>(*bar), std::get<2>(*bar),
                       std::get<3>(*bar), std::get<4>(*bar));
  }

  delete bar;
}

static int32_t GetSyncId(int32_t tree_id) {
  IContentResolver* resolver = GetContentResolver();

  const char *tree_projs[] = {
    TableTree::COLUMN_SYNC_ID,
  };
  unique_ptr<ICursor2> tree_cursor(resolver->Query(
          TableTree::URI, tree_projs, ARRAY_SIZE(tree_projs),
          "%s = '%d'", TableTree::COLUMN_ID, tree_id));
  if (!tree_cursor->MoveToNext()) {
    ZSLOG_ERROR("Nonexistent tree(tree_id:%d)", tree_id);
    return -1;
  }
  return tree_cursor->GetInt32(0);
}

AtomicInt64 TaskMonitor::s_id_(0);

TaskMonitor::TaskMonitor(
    int32_t local_tree_id, int32_t remote_tree_id, 
    StatusType type, int32_t total_files, int64_t total_bytes) {
  type_ = type;
  done_file_ = 0;
  done_byte_ = 0;
  total_file_ = total_files;
  total_byte_ = total_bytes;

  local_tree_id_ = local_tree_id;
  remote_tree_id_ = remote_tree_id;
  ITreeManager *manager = GetTreeManager();
  id_ = s_id_.FetchAndInc(1);
  manager->OnTaskBegin(
      local_tree_id, remote_tree_id, type,
      total_files, total_bytes, id_);
  stat_ = manager->GetTreeStat(local_tree_id);
  pair_stat_ = manager->GetTreePairStat(local_tree_id, remote_tree_id);
  transfer_list_stat_ = manager->GetTransferListStat(local_tree_id);

  sync_id_ = GetSyncId(local_tree_id);
  unique_ptr<Sync> sync(Sync::GetByIdWhereStatusNormal(sync_id_));
  if (sync) {
    sync_type_ = sync->type();
    zs::GetEventNotifier()->NotifySyncStart(
        sync_id_, ToExternalSyncType(sync->type()));
  }else {
    sync_id_ = -1;
  }
}

TaskMonitor::~TaskMonitor() {
  GetTreeManager()->OnTaskEnd(
      local_tree_id_, remote_tree_id_, type_, 
      total_file_ - done_file_, total_byte_ - done_byte_, id_);

  if (sync_id_ != -1) {
    zs::GetEventNotifier()->NotifySyncFinish(sync_id_, ToExternalSyncType(sync_type_));
  }
}

void TaskMonitor::OnFileTransfered(int32_t count) {
  done_file_ += count;

  shared_ptr<ITreeStat> stat = stat_.lock();
  if (stat) {
    stat->OnFileTransfered(type_, count);
  }

  shared_ptr<ITreePairStat> pair_stat = pair_stat_.lock();
  if (pair_stat) {
    pair_stat->OnFileTransfered(type_, count);
  }

  auto ctx = new std::tuple<
      weak_ptr<ITransferList>, StatusType, int32_t, std::string>(
      transfer_list_stat_, type_, id_, active_path_);
  GetEventBaseDb()->DispatchAsync(LambdaOnFileTransfered, ctx, NULL);
}

void TaskMonitor::OnFileSkiped(int32_t count) {
  total_file_-= count;

  shared_ptr<ITreeStat> stat = stat_.lock();
  if (stat) {
    stat->OnFileSkiped(type_, count);
  }

  shared_ptr<ITreePairStat> pair_stat = pair_stat_.lock();
  if (pair_stat) {
    pair_stat->OnFileSkiped(type_, count);
  }

  auto ctx = new std::tuple<
      weak_ptr<ITransferList>, StatusType, int32_t, std::string>(
      transfer_list_stat_, type_, id_, active_path_);
  GetEventBaseDb()->DispatchAsync(LambdaOnFileSkiped, ctx, NULL);
}

void TaskMonitor::OnByteTransfered(int64_t nbytes) {
  done_byte_ += nbytes;

  shared_ptr<ITreeStat> stat = stat_.lock();
  if (stat) {
    stat->OnByteTransfered(type_, nbytes);
  }

  shared_ptr<ITreePairStat> pair_stat = pair_stat_.lock();
  if (pair_stat) {
    pair_stat->OnByteTransfered(type_, nbytes);
  }

  auto ctx = new std::tuple<
      weak_ptr<ITransferList>, StatusType, int64_t, int32_t, std::string>(
      transfer_list_stat_, type_, nbytes, id_, active_path_);
  GetEventBaseDb()->DispatchAsync(LambdaOnByteTransfered, ctx, NULL);
}

void TaskMonitor::OnByteSkiped(int64_t nbytes) {
  total_byte_ -= nbytes;

  shared_ptr<ITreeStat> stat = stat_.lock();
  if (stat) {
    stat->OnByteSkiped(type_, nbytes);
  }

  shared_ptr<ITreePairStat> pair_stat = pair_stat_.lock();
  if (pair_stat) {
    pair_stat->OnByteSkiped(type_, nbytes);
  }

  auto ctx = new std::tuple<
      weak_ptr<ITransferList>, StatusType, int64_t, int32_t, std::string>(
      transfer_list_stat_, type_, nbytes, id_, active_path_);
  GetEventBaseDb()->DispatchAsync(LambdaOnByteSkiped, ctx, NULL);
}

void TaskMonitor::OnFileTransfer(const string &path) {
  shared_ptr<ITreePairStat> pair_stat = pair_stat_.lock();
  if (pair_stat) {
    pair_stat->OnFileTransfer(type_, path);
  }

  active_path_ = path;
}

void TaskMonitor::AppendFile(const string &local_path,
                             const string &remote_path,
                             const string &encode_path,
                             int64_t length) {
  shared_ptr<ITransferList> stat = transfer_list_stat_.lock();
  if (stat) {
    stat->AppendFile(
        local_path, remote_path, encode_path, length, id_);
  }
}

IndexMonitor::IndexMonitor(int32_t tree_id, int32_t total_file_to_index) {
  GetTreeManager()->OnTaskBegin(tree_id, -1, ST_INDEX, total_file_to_index, 0);
  stat_ = GetTreeManager()->GetTreeStat(tree_id);
  tree_id_ = tree_id;
  total_file_to_index_ = total_file_to_index;
  has_indexed_file_ = 0;
}

IndexMonitor::~IndexMonitor() {
  GetTreeManager()->OnTaskEnd(
      tree_id_, -1, ST_INDEX, total_file_to_index_ - has_indexed_file_, 0);
}

void IndexMonitor::OnFileIndexed(int32_t count) {
  has_indexed_file_ += count;
  shared_ptr<ITreeStat> stat = stat_.lock();
  if (stat) {
    stat->OnFileIndexed(count);
  }
}

void IndexMonitor::OnFileWillIndex(const std::string file_indexing) {
  shared_ptr<ITreeStat> stat = stat_.lock();
  if (stat) {
    stat->OnFileWillIndex(file_indexing);
  }
}
}  // namespace zs
