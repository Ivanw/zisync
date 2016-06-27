/**
 * @file test_broadcast.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Test case for BroadcastService.
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

#include <UnitTest++/UnitTest++.h>

#include "zisync_kernel.h"
#include "zisync/kernel/database/broadcast.h"

using zs::BroadcastMessage;
using zs::BroadcastReceiver;
using zs::IBroadcastService;
using zs::IBroadcastMessageIndex;
using zs::IBroadcastMessageUpdate;
using zs::IBroadcastMessageTransfer;
using zs::BroadcastMessageIndex;
using zs::BroadcastMessageUpdate;
using zs::BroadcastMessageTransfer;

class MyReceiver : public BroadcastReceiver {
 public:
  MyReceiver() {
    bm_sync_begin_ = false;
    bm_sync_end_ = false;
    bm_delete_progress_ = false;
    bm_update_progress_ = false;
    bm_index_add_ = false;
    bm_indexe_progress_ = false;
    bm_transfer_begin_ = false;
    bm_transfer_some_ = false;
    bm_transfer_skip_ = false;
    bm_update_begin_ = false;
    bm_update_some_ = false;
    bm_update_skip_ = false;
    bm_invalid_path_ = false;

  }

  virtual void OnReceive(BroadcastMessage msg, void* param) {
    IBroadcastMessageIndex* msg_index;
    IBroadcastMessageUpdate* msg_update;
    IBroadcastMessageTransfer* msg_transfer;
    switch(msg) {
      case zs::BM_SYNC_BEGIN:
        bm_sync_begin_ = true;
        CHECK_EQUAL(1, reinterpret_cast<long>(param));
        break;

      case zs::BM_SYNC_END:
        bm_sync_end_ = true;
        CHECK_EQUAL(1, reinterpret_cast<long>(param));
        break;

      case zs::BM_DELETE_PROGRESS:
        bm_delete_progress_ = true;
        CHECK_EQUAL(2, reinterpret_cast<long>(param));
        break;

      case zs::BM_UPDATE_PROGRESS:
        bm_update_progress_ = true;
        CHECK_EQUAL(2, reinterpret_cast<long>(param));
        break;

      case zs::BM_INDEX_ADD:
        bm_index_add_ = true;
        msg_index = static_cast<IBroadcastMessageIndex*>(param);
        CHECK_EQUAL(1, msg_index->GetTreeId());
        CHECK_EQUAL(2, msg_index->GetCount());
        break;

      case zs::BM_INDEXE_PROGRESS:
        bm_indexe_progress_ = true;
        msg_index = static_cast<IBroadcastMessageIndex*>(param);
        CHECK_EQUAL(1, msg_index->GetTreeId());
        CHECK_EQUAL(2, msg_index->GetCount());
        break;

      case zs::BM_TRANSFER_BEGIN:
        bm_transfer_begin_ = true;
        msg_transfer = static_cast<IBroadcastMessageTransfer*>(param);
        CHECK_EQUAL(1, msg_transfer->GetTreeId());
        CHECK_EQUAL(2, msg_transfer->GetSyncId());
        CHECK_EQUAL(3, msg_transfer->GetBytes());
        break;

      case zs::BM_TRANSFER_SOME:
        bm_transfer_some_ = true;
        msg_transfer = static_cast<IBroadcastMessageTransfer*>(param);
        CHECK_EQUAL(1, msg_transfer->GetTreeId());
        CHECK_EQUAL(2, msg_transfer->GetSyncId());
        CHECK_EQUAL(3, msg_transfer->GetBytes());
        break;

      case zs::BM_TRANSFER_SKIP:
        bm_transfer_skip_ = true;
        msg_transfer = static_cast<IBroadcastMessageTransfer*>(param);
        CHECK_EQUAL(1, msg_transfer->GetTreeId());
        CHECK_EQUAL(2, msg_transfer->GetSyncId());
        CHECK_EQUAL(3, msg_transfer->GetBytes());
        break;

      case zs::BM_UPDATE_BEGIN:
        bm_update_begin_ = true;
        msg_update = static_cast<IBroadcastMessageUpdate*>(param);
        CHECK_EQUAL(1, msg_update->GetTreeId());
        CHECK_EQUAL(2, msg_update->GetSyncId());
        CHECK_EQUAL(3, msg_update->GetCount());
        break;

      case zs::BM_UPDATE_SOME:
        bm_update_some_ = true;
        msg_update = static_cast<IBroadcastMessageUpdate*>(param);
        CHECK_EQUAL(1, msg_update->GetTreeId());
        CHECK_EQUAL(2, msg_update->GetSyncId());
        CHECK_EQUAL(3, msg_update->GetCount());
        break;

      case zs::BM_UPDATE_SKIP:
        bm_update_skip_ = true;
        msg_update = static_cast<IBroadcastMessageUpdate*>(param);
        CHECK_EQUAL(1, msg_update->GetTreeId());
        CHECK_EQUAL(2, msg_update->GetSyncId());
        CHECK_EQUAL(3, msg_update->GetCount());
        break;

      case zs::BM_INVALID_PATH:
        bm_invalid_path_ = true;
        CHECK_EQUAL(NULL, param);
        break;
    }
  }

 protected:
  bool bm_sync_begin_;
  bool bm_sync_end_;
  bool bm_delete_progress_;
  bool bm_update_progress_;
  bool bm_index_add_;
  bool bm_indexe_progress_;
  bool bm_transfer_begin_;
  bool bm_transfer_some_;
  bool bm_transfer_skip_;
  bool bm_update_begin_;
  bool bm_update_some_;
  bool bm_update_skip_;
  bool bm_invalid_path_;
};

TEST(test_BroadcastService) {
  MyReceiver receiver;

  BroadcastMessageIndex msg_index(1, 2);
  BroadcastMessageUpdate msg_update(1, 2, 3);
  BroadcastMessageTransfer msg_transfer(1, 2, 3);

  IBroadcastService* service = zs::GetBroadcastService();

  service->RegisterReceiver(zs::BM_SYNC_BEGIN, &receiver);
  service->RegisterReceiver(zs::BM_SYNC_END, &receiver);
  service->RegisterReceiver(zs::BM_DELETE_PROGRESS, &receiver);
  service->RegisterReceiver(zs::BM_UPDATE_PROGRESS, &receiver);
  service->RegisterReceiver(zs::BM_INDEX_ADD, &receiver);
  service->RegisterReceiver(zs::BM_INDEXE_PROGRESS, &receiver);
  service->RegisterReceiver(zs::BM_TRANSFER_BEGIN, &receiver);
  service->RegisterReceiver(zs::BM_TRANSFER_SOME, &receiver);
  service->RegisterReceiver(zs::BM_TRANSFER_SKIP, &receiver);
  service->RegisterReceiver(zs::BM_UPDATE_BEGIN, &receiver);
  service->RegisterReceiver(zs::BM_UPDATE_SOME, &receiver);
  service->RegisterReceiver(zs::BM_UPDATE_SKIP, &receiver);
  service->RegisterReceiver(zs::BM_INVALID_PATH, &receiver);

  service->PublishBroadcast(zs::BM_SYNC_BEGIN, reinterpret_cast<void*>(1L));
  service->PublishBroadcast(zs::BM_SYNC_END, reinterpret_cast<void*>(1));
  service->PublishBroadcast(zs::BM_DELETE_PROGRESS, reinterpret_cast<void*>(2));
  service->PublishBroadcast(zs::BM_UPDATE_PROGRESS, reinterpret_cast<void*>(2));
  service->PublishBroadcast(zs::BM_INDEX_ADD, &msg_index);
  service->PublishBroadcast(zs::BM_INDEXE_PROGRESS, &msg_index);
  service->PublishBroadcast(zs::BM_TRANSFER_BEGIN, &msg_transfer);
  service->PublishBroadcast(zs::BM_TRANSFER_SOME, &msg_transfer);
  service->PublishBroadcast(zs::BM_TRANSFER_SKIP, &msg_transfer);
  service->PublishBroadcast(zs::BM_UPDATE_BEGIN, &msg_update);
  service->PublishBroadcast(zs::BM_UPDATE_SOME, &msg_update);
  service->PublishBroadcast(zs::BM_UPDATE_SKIP, &msg_update);
  service->PublishBroadcast(zs::BM_INVALID_PATH, NULL);

  service->UnregisterReceiver(zs::BM_SYNC_BEGIN, &receiver);
  service->UnregisterReceiver(zs::BM_SYNC_END, &receiver);
  service->UnregisterReceiver(zs::BM_DELETE_PROGRESS, &receiver);
  service->UnregisterReceiver(zs::BM_UPDATE_PROGRESS, &receiver);
  service->UnregisterReceiver(zs::BM_INDEX_ADD, &receiver);
  service->UnregisterReceiver(zs::BM_INDEXE_PROGRESS, &receiver);
  service->UnregisterReceiver(zs::BM_TRANSFER_BEGIN, &receiver);
  service->UnregisterReceiver(zs::BM_TRANSFER_SOME, &receiver);
  service->UnregisterReceiver(zs::BM_TRANSFER_SKIP, &receiver);
  service->UnregisterReceiver(zs::BM_UPDATE_BEGIN, &receiver);
  service->UnregisterReceiver(zs::BM_UPDATE_SOME, &receiver);
  service->UnregisterReceiver(zs::BM_UPDATE_SKIP, &receiver);
  service->UnregisterReceiver(zs::BM_INVALID_PATH, &receiver);
}

