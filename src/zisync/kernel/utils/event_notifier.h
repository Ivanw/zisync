// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_EVENT_NOTIFIER_H_
#define ZISYNC_KERNEL_UTILS_EVENT_NOTIFIER_H_

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class EventNotifier {
  friend EventNotifier* GetEventNotifier();
 public:
  ~EventNotifier() {}
  void SetEventListner(IEventListener *listener);
  void NotifySyncStart(int32_t sync_id, int32_t sync_type);
  void NotifySyncFinish(int32_t sync_id, int32_t sync_type);
  void NotifySyncModify();
  void NotifyBackupModify();
  void NotifyDownloadFileNumber(int num);
  void NotifyIndexStart(int32_t sync_id, int32_t sync_type);
  void NotifyIndexFinish(int32_t sync_id, int32_t sync_type);

 protected:
  IEventListener *listener;
 private:
  EventNotifier():listener(NULL) {}

  static EventNotifier event_notifier;
};

void EventNotify();

EventNotifier* GetEventNotifier();

}  // namespace zs


#endif  // ZISYNC_KERNEL_UTILS_EVENT_NOTIFIER_H_
