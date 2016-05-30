//
//  KernelStats.h
//  PlusyncMac
//
//  Created by Zi on 6/7/15.
//  Copyright (c) 2015 plusync. All rights reserved.
//
#import <Foundation/Foundation.h>
#include "zisync_kernel.h"
#include "zisync/kernel/utils/discover_device.h"

#ifndef PLUSYNCMAC_UTILS_H_
#define PLUSYNCMAC_UTILS_H_

class MsgStat;

enum ZSChangeType {
  ZSChangeTypeInsert,
  ZSChangeTypeDelete,
  ZSChangeTypeUpdate
};

@protocol ListItemChangeTypeInterface <NSObject>
@property (nonatomic) ZSChangeType changeType;
@end

@interface FileMetaObjc : NSObject
- (id) initWithFileMeta: (const zs::FileMeta &) fileMeta;
- (id) initWithMsgStat: (const zs::MsgStat&) msgStat syncUuid:(const std::string&)uuid  superDirectory:(const std::string&)superdir;
  
@property (strong, nonatomic) NSString *name;
@property (nonatomic) zs::FileMetaType type;
@property (nonatomic) int64_t length;
@property (nonatomic) int64_t mtime;
@property (nonatomic) BOOL has_download_cache;
@property (strong, nonatomic) NSString *cache_path;
@end

@interface ListSyncResultObjc : NSObject
- (id) initWithListSyncResult: (const zs::ListSyncResult *)listSync;

@property (strong, nonatomic) NSArray *files;
@end




@interface DeviceInfoObjc : NSObject
@property (nonatomic) int32_t device_id;
@property (nonatomic, strong) NSString *device_name;
@property (nonatomic) int device_type;
@property (nonatomic) BOOL is_mine;
@property (nonatomic) BOOL is_backup;
@property (nonatomic) BOOL is_online;
@property (nonatomic) BOOL is_shared;
@property (nonatomic, strong) NSString *backup_root;

@property (nonatomic) int32_t version;
@property (nonatomic, strong) NSString *ip;

@property (nonatomic) int32_t discover_port;
@property (nonatomic) BOOL is_static_peer;

//not always available, only valid when init with discovereddevice
@property (nonatomic, strong) NSString *uuid;

- (id)initWithDeviceInfo: (const zs::DeviceInfo &)obj;
- (id)initWithDiscoveredDevice:(const zs::DiscoveredDevice&)obj;
- (BOOL)isEqual:(id)object;
+ (NSComparator)comparatorForSortingWithId;
+ (NSComparator)comparatorForSortingWithUuid;
@end



@interface TreeInfoObjc : NSObject
@property (nonatomic) int32_t tree_id;
@property (nonatomic) NSString *tree_uuid;
@property (nonatomic) NSString *tree_root;
@property (strong, nonatomic) DeviceInfoObjc *device;
@property (nonatomic) BOOL is_local;
@property (nonatomic) BOOL is_sync_enabled;
@property (nonatomic) zs::TreeRootStatus root_status;

- (id)initWithTreeInfo:(const zs::TreeInfo&)obj;
- (void)doInitTreeId:(int32_t)tree_id
                uuid:(NSString*)uuid
                root:(NSString*)root
              device:(DeviceInfoObjc*)device
             isLocal:(BOOL)isLocal
       isSyncEnabled:(BOOL)isSyncEnabled
          rootStatus:(zs::TreeRootStatus)status;

+ (NSComparator)comparatorForSortingTreeInfoWithId;
- (BOOL)isEqual:(id)object;
@end



@interface ShareSyncInfoObjc : NSObject
@property (nonatomic, strong) DeviceInfoObjc *device;
@property (nonatomic) int32_t sync_perm;

- (id)initWithShareSyncInfoObjc:(const zs::ShareSyncInfo &)obj;
- (BOOL)isEqual:(id)object;
+ (NSComparator)comparatorForSortingShareSyncsWithDeviceId;
@end



@interface SyncInfoObjc : NSObject<ListItemChangeTypeInterface>
@property (nonatomic) int32_t sync_id;
@property (nonatomic, strong) NSString *sync_uuid;
@property (nonatomic, strong) NSString *sync_name;
@property (nonatomic) int32_t sync_perm;
@property (nonatomic) int64_t last_sync;
@property (nonatomic) BOOL is_share;
@property (nonatomic, strong) DeviceInfoObjc *creator;
@property (nonatomic, strong) NSArray *trees;
@property (nonatomic, strong) NSArray *share_syncs;

- (id)initWithSyncInfo:(const zs::SyncInfo&)obj;
- (BOOL)isEqual:(id)object;
+ (NSComparator)comparatorForSortingWithSyncId;
@end



@interface BackupInfoObjc : NSObject<ListItemChangeTypeInterface>
@property (nonatomic) int32_t backup_id;
@property (nonatomic) NSString *backup_name;
@property (nonatomic) int64_t last_sync;
@property (nonatomic) TreeInfoObjc *src_tree;
@property (nonatomic, strong) NSArray *target_trees;

- (id)initWithBackupInfo:(const zs::BackupInfo&)obj;
- (BOOL)isEqual:(id)object;
+ (NSComparator)comparatorForSortingWithBackupId;
@end



@interface TreeStatusObjc : NSObject
@property (nonatomic) int32_t tree_id;
@property (nonatomic) int32_t num_file_to_index;
@property (nonatomic) int32_t num_file_to_download;
@property (nonatomic) int64_t num_byte_to_download;
@property (nonatomic) int64_t speed_download;
@property (nonatomic) int32_t num_file_to_upload;
@property (nonatomic) int64_t num_byte_to_upload;
@property (nonatomic) int64_t speed_upload;
@property (nonatomic) BOOL is_transfering;
@property (nonatomic, strong) NSString *file_indexing;

- (id)initWithTreeStatus:(const zs::TreeStatus&)obj;
- (BOOL)isEqual:(id)object;
@end



@interface TreePairStatusObjc : NSObject
@property (nonatomic) int32_t local_tree_id;
@property (nonatomic) int32_t remote_tree_id;
@property (nonatomic) int32_t static_num_file_to_download;
@property (nonatomic) int32_t static_num_file_to_upload;
@property (nonatomic) int64_t static_num_byte_to_download;
@property (nonatomic) int64_t static_num_byte_to_upload;
@property (nonatomic) int32_t static_num_file_consistent;
@property (nonatomic) int64_t static_num_byte_consistent;

@property (nonatomic) int32_t num_file_to_download;
@property (nonatomic) int32_t num_file_to_upload;
@property (nonatomic) int64_t num_byte_to_download;
@property (nonatomic) int64_t num_byte_to_upload;
@property (nonatomic) int64_t speed_download;
@property (nonatomic) int64_t speed_upload;
@property (nonatomic, strong) NSString *download_file;
@property (nonatomic, strong) NSString *upload_file;
@property (nonatomic) BOOL is_transfering;

- (id)initWithTreePairStatus:(const zs::TreePairStatus&)obj;
- (BOOL)isEqual:(id)object;
@end



@interface FileTransferStatObjc : NSObject
@property (nonatomic, strong) NSString *local_path;
@property (nonatomic, strong) NSString *remote_path;
@property (nonatomic) int64_t bytes_file_size;
@property (nonatomic) int64_t bytes_to_transfer;
@property (nonatomic) int64_t speed;
@property (nonatomic) zs::FileTransferStatus transfer_status;

- (id)initWithFileTransferStat:(const zs::FileTransferStat&)obj;
- (BOOL)isEqual:(id)object;
@end



@interface TransferListStatusObjc : NSObject
@property (nonatomic, strong) NSArray *list_;
@property (nonatomic) int32_t tree_id;
- (id)initWithTransferListStatus:(const zs::TransferListStatus&)obj treeId:(int32_t)tree_id_;
@end

@interface IpPortObjc : NSObject
@property (nonatomic, strong) NSString *ip;
@property (nonatomic) int32_t port;
- (id)initWithIpPort:(const zs::IpPort&)ipport;
@end

@interface ListStaticPeersObjc : NSObject
@property (nonatomic, strong) NSArray *peers;
- (id)initWithListStaticPeers:(const zs::ListStaticPeers &)listStaticPeers;
@end

@interface DownloadStatusObjc : NSObject
@property (nonatomic) int status;
@property (nonatomic) int64_t total_num_byte;
@property (nonatomic) int64_t num_byte_to_download;
@property (nonatomic) int64_t speed_download;
@property (nonatomic) zs::err_t error_code;

- (id)initWithDownloadStatus:(const zs::DownloadStatus &)downloadStatus;
@end


@interface UploadStatusObjc : NSObject
@property (nonatomic) int status;
@property (nonatomic) int64_t total_num_byte;
@property (nonatomic) int64_t num_byte_to_upload;
@property (nonatomic) int64_t speed_upload;
@property (nonatomic) zs::err_t error_code;

- (id)initWithUploadStatus:(const zs::UploadStatus &)uploadStatus;
@end
#endif
