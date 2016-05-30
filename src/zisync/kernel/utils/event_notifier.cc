// Copyright 2014, zisync.com
#include <map>

#include "zisync/kernel/platform/platform.h"
#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/event_notifier.h"
#include "zisync/kernel/zslog.h"

namespace zs {

using std::map;

static map<int32_t, int> syncing;
static Mutex sync_mutex;
EventNotifier EventNotifier::event_notifier;

// static Mutex modify_mutex;
// int64_t last_notify_sync_modify_time = -1;
// int64_t last_notify_backup_modify_time = -1;

void EventNotifier::SetEventListner(IEventListener *listener_) {
  listener = listener_;
}

void EventNotifier::NotifySyncStart(int32_t sync_id, int32_t type) {
  if (listener == NULL) {
    return;
  }
  MutexAuto mutex(&sync_mutex);
  int32_t &sync_num = syncing[sync_id];
  if (sync_num == 0) {
    listener->NotifySyncStart(sync_id, type);
  }
  sync_num ++;
}

void EventNotifier::NotifySyncFinish(int32_t sync_id, int32_t type) {
  if (listener == NULL) {
    return;
  }
  MutexAuto mutex(&sync_mutex);
  int32_t &sync_num = syncing[sync_id];
  if (sync_num == 1) {
    listener->NotifySyncFinish(sync_id, type);
  }
  sync_num --;
}
  
void EventNotifier::NotifySyncModify() {
  if (listener == NULL) {
    return;
  }
  listener->NotifySyncModify();
}

void EventNotifier::NotifyBackupModify() {
  if (listener == NULL) {
    return;
  }
  listener->NotifyBackupModify();
}

void EventNotifier::NotifyDownloadFileNumber(int num) {
  if (listener == NULL) {
    return;
  }
  listener->NotifyDownloadFileNumber(num);
}

static map<int32_t, int> indexing;
static Mutex indexing_mutex;

void EventNotifier::NotifyIndexStart(int32_t sync_id, int32_t type) {
  assert(sync_id != -1);
  MutexAuto mutx(&indexing_mutex);
  int &indexing_cnt = indexing[sync_id];
  if (indexing_cnt++ == 0 && listener) {
    listener->NotifyIndexStart(sync_id, type);
  }
}

void EventNotifier::NotifyIndexFinish(int32_t sync_id, int32_t type) {
  assert(sync_id != -1);
  MutexAuto mutx(&indexing_mutex);
  int &indexing_cnt = indexing[sync_id];
  if (--indexing_cnt == 0 && listener) {
    listener->NotifyIndexFinish(sync_id, type);
  }
}

EventNotifier* GetEventNotifier() {
  return &EventNotifier::event_notifier;
}

}  // namespace zs

