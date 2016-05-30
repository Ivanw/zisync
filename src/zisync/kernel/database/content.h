// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_DATABASE_CONTENT_H_
#define ZISYNC_KERNEL_DATABASE_CONTENT_H_

#include "zisync_kernel.h"  // NOLINT

namespace zs {

class MsgSync;
class MsgDevice;
class MsgTree;
class MsgRemoteMeta;
class Uri;
class FileStat;
class OperationList;

err_t DatabaseInit();
err_t SaveDiscoverPort(int32_t port);
err_t SaveRoutePort(int32_t port);
err_t SaveDataPort(int32_t port);
err_t SaveAccount(const char *username, const char *password);
err_t SaveDeviceName(const char *device_name);
// bool IsBackupTargetNormal(int32_t backup_id, int32_t device_id);
err_t SaveBackupRoot(const char *device_name);
err_t SaveTreeRootPrefix(const char *tree_root_prefix);
err_t SaveReportHost(const std::string &report_host);
err_t SaveCAcert(const std::string &ca_cert);
err_t SaveMacToken(const std::string &mac_token);
/*  if sync_uuid.size == 0 , means create a new sync_uuid */
#ifdef ZS_TEST
err_t RemoteTreeAdd(const std::string& tree_uuid, const std::string& device_uuid, 
                    const std::string& sync_uuid, TreeInfo *tree_info);
#endif
err_t CheckSyncInContentById(int32_t sync_id);
void SetTreesInMsgSync(int32_t sync_id, MsgSync *sync);
err_t SetTreesAndSyncModeInMsgSyncBackupSrc(
    int32_t sync_id, int32_t remote_device_id, MsgSync *sync);
void SetTreesInMsgSyncBackupDst(int32_t sync_id, MsgSync *sync);

int32_t StoreDeviceIntoDatabase(
    const MsgDevice &device, const char *host, bool is_mine, bool is_ipv6,
    bool is_share);
// int32_t StoreSyncIntoDatabase(
//     int32_t device_id, const MsgSync &sync, bool ignore_deleted = true);
int32_t StoreTreeIntoDatabase(
    int32_t device_id, int32_t sync_id, const MsgTree &tree, 
    int32_t tree_backup_type);

err_t StoreRemoteMeta(
    int32_t sync_id, const char *remote_tree_uuid, 
    const MsgRemoteMeta &response);
void AddFileStatIntoOpList(const Uri &file_uri, const FileStat &file_stat,
                    OperationList *op_list);
void GetAllOnlineDeviceUri(std::vector<std::string> *uris);
void SetDeviceMetaInMsgDevice(MsgDevice *device);
void SetBackupRootInMsgDevice(int32_t remote_device_id, MsgDevice *device);

void SetMsgDeviceForMyDevice(MsgDevice *device);
void UpdateLastSync(int32_t sync_id);
void InitDeviceStatus();
void UpdateEarliestNoRespTimeInDatabase(
    int32_t device_id, const char *device_ip, int64_t time);

void IssueDiscover();
void IssueDiscoverBroadcast();
void IssueSyncWithDevice(int32_t device_id);
err_t ShareSync(int32_t discover_id, int32_t device_id, int32_t sync_id);
/* also add SyncMode */
void AddBackupInMsgDevice(MsgDevice *device, int32_t device_id);
void AddShareSyncInMsgDevice(MsgDevice *device, int32_t remote_device_id);

err_t SetSyncMode(
    int32_t local_tree_id, int32_t remote_tree_id, int sync_mode, 
    int32_t sync_time_in_s);
void GetSyncMode(
    int32_t local_tree_id, int32_t local_tree_type, 
    int32_t remote_tree_id, int *sync_mode, int32_t *sync_time_in_s);
err_t SetShareSyncPerm(
    int32_t device_id, int32_t sync_id, int32_t sync_perm);
err_t DelShareSync(
    int32_t device_id, int32_t sync_id);
err_t GetShareSyncPerm(
    int32_t device_id, int32_t sync_id, int32_t *sync_perm = NULL);

void TokenChangeToDiff(
    const std::string &sync_where, const std::string &device_where);
void RemoteDeviceTokenChangeToDiff(int32_t device_id, int32_t route_port);
void CheckAndDeleteNoIpLeftDevice(int32_t device_id);
bool GetListableDevice(int32_t device_id, int32_t *route_port);
inline bool IsListableDevice(int32_t device_id) {
  return GetListableDevice(device_id, NULL);
}

}  // namespace zs
#endif  // ZISYNC_KERNEL_DATABASE_CONTENT_H_
