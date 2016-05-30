/**
 * @file broadcast.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Broadcast implmentation.
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
#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/broadcast.h"

namespace zs {

BroadcastReceiver::BroadcastReceiver(
    IHandler* handler /*= NULL*/, bool auto_delete /*= TRUE*/) {
  handler_ = handler;
  auto_delete_ = auto_delete;
}

BroadcastReceiver::~BroadcastReceiver() {
  if (auto_delete_ && handler_) {
    delete handler_;
    handler_ = NULL;
  }
}

void BroadcastReceiver::DispatchBroadcastMessage(
    BroadcastMessage msg, void* param) {
  class OnChangeRunable : public IRunnable {
   public:
    OnChangeRunable(
        BroadcastReceiver* receiver, BroadcastMessage msg, void* param) {
      receiver_ = receiver;
      msg_ = msg;
      param_ = param;
    }

    virtual err_t Run() {
      if (receiver_) {
        receiver_->OnReceive(msg_, param_);
        return zs::ZISYNC_SUCCESS;
      }
      return zs::ZISYNC_ERROR_GENERAL;
    }

   private:
    BroadcastReceiver* receiver_;
    BroadcastMessage msg_;
    void* param_;
  };


  if (!handler_) {
    OnReceive(msg, param);
  } else {
    handler_->Post(new OnChangeRunable(this, msg, param));
  }
}

BroadcastService BroadcastService::s_hInstance;

BroadcastService::BroadcastService() {
  mutex_ = new OsMutex();
  assert(mutex_);

  mutex_->Initialize();
}

BroadcastService::~BroadcastService() {
  mutex_->CleanUp();

  delete mutex_;
}

bool BroadcastService::RegisterReceiver(
    BroadcastMessage msg, BroadcastReceiver* receiver) {
  MutexAuto autor(mutex_);

  auto it = receiver_hashtable_.find(msg);
  if (it != receiver_hashtable_.end()) {
    it->second.push_back(receiver);
  } else {
    receiver_hashtable_[msg].push_back(receiver);
  }

  return true;
}

bool BroadcastService::UnregisterReceiver(
    BroadcastMessage msg, BroadcastReceiver* receiver) {
  MutexAuto autor(mutex_);

  auto it = receiver_hashtable_.find(msg);
  if (it != receiver_hashtable_.end()) {
    std::vector<BroadcastReceiver*>& v = it->second;
    for (auto ii = v.begin(); ii != v.end(); ) {
      if ((*ii) == receiver) {
        ii = v.erase(ii);
      } else {
        ii++;
      }
    }
  }

  return true;
}

bool BroadcastService::PublishBroadcast(BroadcastMessage msg, void* param) {
  MutexAuto autor(mutex_);

  auto it = receiver_hashtable_.find(msg);
  if (it != receiver_hashtable_.end()) {
    std::vector<BroadcastReceiver*>& v = it->second;
    for (auto ii = v.begin(); ii != v.end(); ii++) {
      (*ii)->DispatchBroadcastMessage(msg, param);
    }
  }

  return true;
}

IBroadcastService* GetBroadcastService() {
  return &BroadcastService::s_hInstance;
}

}  // namespace zs
