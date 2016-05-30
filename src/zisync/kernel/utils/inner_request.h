// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_INNER_REQUEST_H_
#define ZISYNC_KERNEL_UTILS_INNER_REQUEST_H_

#include <stdint.h>

#include "zisync_kernel.h"  // NOLINT

namespace zs {

void IssuePushDeviceMeta(int32_t device_id = -1);
void IssueSyncWithRemoteTree(int32_t sync_id, int32_t tree_id);
void IssueSyncWithLocalTree(int32_t sync_id, int32_t tree_id);
void IssueRefresh(int32_t tree_id);
void IssueRefreshWithSyncId(int32_t sync_id);
void IssueAllRefresh();
void IssuePushSyncInfo(int32_t sync_id, int32_t device_id = -1);
void IssuePushTreeInfo(int32_t tree_id);
void IssuePushDeviceInfo(int32_t device_id);
void IssueEraseAllPeer(const char* account_name);
void IssueSync(int32_t local_tree_id, int32_t remote_tree_id);
void IssueSyncWithDevice(int32_t device_id);
err_t IssueRouteStartup();
err_t IssueRouteShutdown();
void IssueDiscoverSetBackground();
void IssueDiscoverSetForeground();
void IssueTokenChanged(const std::string &origin_token);
void IssueErasePeer(int32_t device_id, int32_t route_port);

}  // namespace zs

#endif  //  ZISYNC_KERNEL_UTILS_INNER_REQUEST_H_
