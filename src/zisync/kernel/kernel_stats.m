//
//  KernelStats.m
//  PlusyncMac
//
//  Created by Zi on 6/7/15.
//  Copyright (c) 2015 plusync. All rights reserved.
//
#import "zisync/kernel/kernel_stats.h"
#import "PLUtils.h"
#include "zisync/kernel/proto/kernel.pb.h"
#include "zisync_kernel.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/platform/platform_mac.h"
#include "zisync/kernel/utils/download.h"

@implementation FileMetaObjc
- (id) initWithFileMeta: (const zs::FileMeta &) fileMeta {
  self = [super init];
  if (self) {
    _name = [NSString stringWithUTF8String:fileMeta.name.c_str()];
    _type = fileMeta.type;
    _length = fileMeta.length;
    _mtime = fileMeta.mtime;
    _has_download_cache = fileMeta.has_download_cache;
    _cache_path = [NSString stringWithUTF8String:fileMeta.cache_path.c_str()];
  }
  return self;
}

- (id) initWithMsgStat:(const zs::MsgStat &)msgStat
              syncUuid:(const std::string &)uuid
        superDirectory:(const std::string &)superdir{
  self = [super init];
  if (self) {
    const char *name = msgStat.path().c_str() + superdir.length();
    _name = [NSString stringWithUTF8String:name];
    _type = msgStat.type() == zs::FT_REG ? zs::FILE_META_TYPE_REG : zs::FILE_META_TYPE_DIR;
    _length = msgStat.length();
    _mtime = msgStat.mtime();
    _cache_path = [NSString stringWithUTF8String: zs::GenDownloadTmpPath(uuid, msgStat.path()).c_str()];
    _has_download_cache = zs::IDownload::GetInstance()->
      HasDownloadCache(_cache_path.UTF8String, _mtime, _length);
  }
  
  return self;
}
@end

@implementation ListSyncResultObjc

- (id) initWithListSyncResult: (const zs::ListSyncResult *)listSync {
  self = [super init];
  if (self) {
    NSMutableArray *temp = [NSMutableArray arrayWithCapacity:listSync->files.size()];
    for(auto it = listSync->files.begin(); it != listSync->files.end(); ++it) {
      [temp addObject:[[FileMetaObjc alloc] initWithFileMeta:*it]];
    }
    _files = temp;
  }
  return self;
}

@end


@implementation DeviceInfoObjc

- (id)initWithDeviceInfo:(const zs::DeviceInfo &)obj {
  self = [super init];
  if (self) {
    _device_id = obj.device_id;
    _device_name = [NSString stringWithUTF8String:obj.device_name.c_str()];
    _device_type = obj.device_type;
    _is_mine = obj.is_mine;
    _is_online = obj.is_online;
    _is_shared = obj.is_shared;
    _backup_root = [NSString stringWithUTF8String:obj.backup_root.c_str()];
    _version = obj.version;
    _ip = [NSString stringWithUTF8String:obj.ip.c_str()];
    _discover_port = obj.discover_port;
    _is_static_peer = obj.is_static_peer;
  }
  return self;
}

- (id)initWithDiscoveredDevice:(const zs::DiscoveredDevice &)obj {
  self = [super init];
  if (self) {
    _device_id = obj.id;
    _device_name = [NSString stringWithUTF8String:obj.name.c_str()];
    _device_type = obj.type;
    _is_mine = obj.is_mine;
    _is_shared = obj.is_shared;
    _ip = [NSString stringWithUTF8String:obj.ip.c_str()];
    _discover_port = obj.discover_port;
    _is_static_peer = obj.is_static_peer;
    _uuid = [NSString stringWithUTF8String:obj.uuid.c_str()];
  }
  return self;
}

- (BOOL)isEqual:(id)object {
  if (self == object) {
    return YES;
  }
  
  if (!object || ![object isKindOfClass:[DeviceInfoObjc class]]) {
    return NO;
  }
  
  DeviceInfoObjc *other = (DeviceInfoObjc*)object;
  return \
    self.device_id == other.device_id &&
    [self.device_name isEqualToString:other.device_name] &&
    self.device_type == other.device_type &&
    IsEqualBOOL(self.is_mine, other.is_mine) &&
    IsEqualBOOL(self.is_backup, other.is_backup) &&
    IsEqualBOOL(self.is_online, other.is_online) &&
    IsEqualBOOL(self.is_shared, other.is_shared) &&
    [self.backup_root isEqualToString:other.backup_root] &&
    self.version == other.version &&
    [self.ip isEqualToString:other.ip] &&
    self.discover_port == other.discover_port &&
    IsEqualBOOL(self.is_static_peer, other.is_static_peer);
}

+ (NSComparator)comparatorForSortingWithId {
  return ^(id obj1, id obj2) {
    DeviceInfoObjc *d1 = (DeviceInfoObjc*)obj1;
    DeviceInfoObjc *d2 = (DeviceInfoObjc*)obj2;
    if (d1.device_id < d2.device_id) {
      return NSOrderedAscending;
    }else if (d1.device_id == d2.device_id) {
      return NSOrderedDescending;
    }else {
      return NSOrderedSame;
    }
  };
}

+ (NSComparator)comparatorForSortingWithUuid {
  return ^(id obj1, id obj2) {
    DeviceInfoObjc *d1 = (DeviceInfoObjc*)obj1;
    DeviceInfoObjc *d2 = (DeviceInfoObjc*)obj2;
    return [d1.uuid compare:d2.uuid];
  };
}

@end


@implementation TreeInfoObjc

- (id)initWithTreeInfo:(const zs::TreeInfo &)obj {
  self = [super init];
  if (self) {
//    _tree_id = obj.tree_id;
//    _tree_uuid = [NSString stringWithUTF8String:obj.tree_uuid.c_str()];
//    _tree_root = [NSString stringWithUTF8String:obj.tree_root.c_str()];
//    _device = [[DeviceInfoObjc alloc] initWithDeviceInfo:obj.device];
//    _is_local = obj.is_local;
//    _root_status = obj.root_status;
    [self doInitTreeId:obj.tree_id
                  uuid:[NSString stringWithUTF8String:obj.tree_uuid.c_str()]
                  root:[NSString stringWithUTF8String:obj.tree_root.c_str()]
                device:[[DeviceInfoObjc alloc] initWithDeviceInfo:obj.device]
               isLocal:obj.is_local
         isSyncEnabled:obj.is_sync_enabled
            rootStatus:obj.root_status];
  }
  return self;
}

- (void)doInitTreeId:(int32_t)tree_id
                uuid:(NSString *)uuid
                root:(NSString *)root
              device:(DeviceInfoObjc *)device
             isLocal:(BOOL)isLocal
       isSyncEnabled:(BOOL)isSyncEnabled
          rootStatus:(zs::TreeRootStatus)status {
  _tree_id = tree_id;
  _tree_uuid = uuid;
  _tree_root = root;
  _device = device;
  _is_local = isLocal;
  _is_sync_enabled = isSyncEnabled;
  _root_status = status;
}

- (BOOL) isEqual:(id)object {
  if (self == object) {
    return YES;
  }
  
  if (!object || ![object isKindOfClass:[TreeInfoObjc class]]) {
    return NO;
  }
  
  TreeInfoObjc *other = (TreeInfoObjc*)object;
  
  return \
    self.tree_id == other.tree_id &&
    [self.tree_uuid isEqualToString:other.tree_uuid] &&
    [self.tree_root isEqualToString:other.tree_root] &&
    [self.device isEqual: other.device] &&
    IsEqualBOOL(self.is_local, other.is_local) &&
    IsEqualBOOL(self.is_sync_enabled, other.is_sync_enabled) &&
    self.root_status == other.root_status;
}

+ (NSComparator)comparatorForSortingTreeInfoWithId {
  return ^(id obj1, id obj2) {
    TreeInfoObjc *t1 = (TreeInfoObjc*)obj1;
    TreeInfoObjc *t2 = (TreeInfoObjc*)obj2;
    if (t1.tree_id < t2.tree_id) {
      return NSOrderedAscending;
    }else if (t1.tree_id == t2.tree_id) {
      return NSOrderedDescending;
    }else {
      return NSOrderedSame;
    }
  };
}

@end


@implementation ShareSyncInfoObjc

- (id)initWithShareSyncInfoObjc:(const zs::ShareSyncInfo &)obj {
  self = [super init];
  if (self) {
    _device = [[DeviceInfoObjc alloc] initWithDeviceInfo:obj.device];
    _sync_perm = obj.sync_perm;
  }
  return self;
}

- (BOOL)isEqual:(id)object {
  if (self == object) {
    return YES;
  }
  if (!object || ![object isKindOfClass:[ShareSyncInfoObjc class]]) {
    return NO;
  }
  
  ShareSyncInfoObjc *other = (ShareSyncInfoObjc*)object;
  return \
    [self.device isEqual:other.device] &&
    self.sync_perm == other.sync_perm;
}

+ (NSComparator)comparatorForSortingShareSyncsWithDeviceId {
  return ^(id obj1, id obj2) {
    ShareSyncInfoObjc *t1 = (ShareSyncInfoObjc*)obj1;
    ShareSyncInfoObjc *t2 = (ShareSyncInfoObjc*)obj2;
    if (t1.device.device_id < t2.device.device_id) {
      return NSOrderedAscending;
    }else if (t1.device.device_id == t2.device.device_id) {
      return NSOrderedDescending;
    }else {
      return NSOrderedSame;
    }
  };
}
@end


@implementation SyncInfoObjc
@synthesize changeType;

- (id)initWithSyncInfo:(const zs::SyncInfo &)obj {
  self = [super init];
  if (self) {
    _sync_id = obj.sync_id;
    _sync_uuid = [NSString stringWithUTF8String:obj.sync_uuid.c_str()];
    _sync_name = [NSString stringWithUTF8String:obj.sync_name.c_str()];
    _sync_perm = obj.sync_perm;
    _last_sync = obj.last_sync;
    _is_share = obj.is_share;
    _creator = [[DeviceInfoObjc alloc] initWithDeviceInfo:obj.creator];
    
    NSMutableArray *trees_tmp = [NSMutableArray arrayWithCapacity:obj.trees.size()];
    for(auto it = obj.trees.begin(); it != obj.trees.end(); ++it) {
      [trees_tmp addObject:[[TreeInfoObjc alloc] initWithTreeInfo:*it]];
    }
    _trees = trees_tmp;
    
    NSMutableArray *shared_syncs_temp = [NSMutableArray arrayWithCapacity:obj.share_syncs.size()];
    for(auto it = obj.share_syncs.begin(); it != obj.share_syncs.end(); ++it) {
      [shared_syncs_temp addObject:[[ShareSyncInfoObjc alloc] initWithShareSyncInfoObjc:*it]];
    }
    _share_syncs = shared_syncs_temp;
    
  }
  return self;
}

- (BOOL)isEqual:(id)object {
  if (self == object) {
    return YES;
  }
  if (!object || ![object isKindOfClass:[SyncInfoObjc class]]) {
    return NO;
  }
  
  SyncInfoObjc *other = (SyncInfoObjc*)object;
  NSAssert([self.trees isKindOfClass:[NSMutableArray class]], nil);
  NSAssert([other.trees isKindOfClass:[NSMutableArray class]], nil);
  NSMutableArray *this_trees = (NSMutableArray*)self.trees;
  NSMutableArray *other_trees = (NSMutableArray*)other.trees;
  [this_trees sortUsingComparator:[TreeInfoObjc comparatorForSortingTreeInfoWithId]];
  [other_trees sortUsingComparator:[TreeInfoObjc comparatorForSortingTreeInfoWithId]];
  
  NSAssert([self.share_syncs isKindOfClass:[NSMutableArray class]], nil);
  NSAssert([other.share_syncs isKindOfClass:[NSMutableArray class]], nil);
  NSMutableArray *thisShareSyncs = (NSMutableArray*)self.share_syncs;
  NSMutableArray *otherShareSyncs = (NSMutableArray*)other.share_syncs;
  [thisShareSyncs sortUsingComparator:[ShareSyncInfoObjc comparatorForSortingShareSyncsWithDeviceId]];
  [otherShareSyncs sortUsingComparator:[ShareSyncInfoObjc comparatorForSortingShareSyncsWithDeviceId]];
  
  return \
    self.sync_id == other.sync_id &&
    [self.sync_uuid isEqualToString:other.sync_uuid] &&
    [self.sync_name isEqualToString:other.sync_name] &&
    self.sync_perm == other.sync_perm &&
    self.last_sync == other.last_sync &&
    IsEqualBOOL(self.is_share, other.is_share) &&
    [self.creator isEqual:other.creator] &&
    [self.trees isEqual:other.trees] &&
    [self.share_syncs isEqual:other.share_syncs];
}

+ (NSComparator)comparatorForSortingWithSyncId {
  return ^(id obj1, id obj2) {
    SyncInfoObjc *s1 = (SyncInfoObjc*)obj1;
    SyncInfoObjc *s2 = (SyncInfoObjc*)obj2;
    if (s1.sync_id < s2.sync_id) {
      return NSOrderedAscending;
    }else if (s1.sync_id == s2.sync_id) {
      return NSOrderedSame;
    }else {
      return NSOrderedDescending;
    }
  };
}

@end


@implementation BackupInfoObjc
@synthesize changeType;

- (id)initWithBackupInfo:(const zs::BackupInfo &)obj {
  self = [super init];
  if (self) {
    _backup_id = obj.backup_id;
    _backup_name = [NSString stringWithUTF8String:obj.backup_name.c_str()];
    _last_sync = obj.last_sync;
    _src_tree = [[TreeInfoObjc alloc] initWithTreeInfo:obj.src_tree];
    
    NSMutableArray *targets = [NSMutableArray arrayWithCapacity:obj.target_trees.size()];
    for(auto it = obj.target_trees.begin(); it != obj.target_trees.end(); ++it) {
      [targets addObject:[[TreeInfoObjc alloc] initWithTreeInfo:*it]];
    }
    _target_trees = targets;
  }
  
  return self;
}

- (BOOL)isEqual:(id)object {
  if (self == object) {
    return YES;
  }
  if (!object || ![object isKindOfClass:[BackupInfoObjc class]]) {
    return NO;
  }
  
  BackupInfoObjc *other = (BackupInfoObjc*)object;
  return \
    self.backup_id == other.backup_id &&
    [self.backup_name isEqualToString:other.backup_name] &&
    self.last_sync == other.last_sync &&
    [self.src_tree isEqual:other.src_tree] &&
    [self.target_trees isEqual:other.target_trees];
}

+ (NSComparator)comparatorForSortingWithBackupId {
  return ^(id obj1, id obj2) {
    BackupInfoObjc *b1 = (BackupInfoObjc*)obj1;
    BackupInfoObjc *b2 = (BackupInfoObjc*)obj2;
    if (b1.backup_id < b2.backup_id) {
      return NSOrderedAscending;
    }else if (b1.backup_id == b2.backup_id) {
      return NSOrderedSame;
    }else {
      return NSOrderedDescending;
    }
  };
}
@end


@implementation TreeStatusObjc

- (id)initWithTreeStatus:(const zs::TreeStatus &)obj {
  self = [super init];
  if (self) {
    _tree_id = obj.tree_id;
    _num_file_to_index = obj.num_file_to_index;
    _num_file_to_download = obj.num_file_to_download;
    _num_byte_to_download = obj.num_byte_to_download;
    _speed_download = obj.speed_download;
    _num_file_to_upload = obj.num_file_to_upload;
    _num_byte_to_upload = obj.num_byte_to_upload;
    _speed_upload = obj.speed_upload;
    _is_transfering = obj.is_transfering;
  }
  return self;
}

- (BOOL)isEqual:(id)object {
  if (self == object) {
    return YES;
  }
  if (!object || ![object isKindOfClass:[TreeStatusObjc class]]) {
    return NO;
  }
  
  TreeStatusObjc *other = (TreeStatusObjc*)object;
  return
    self.tree_id == other.tree_id &&
    self.num_file_to_index == other.num_file_to_index &&
    self.num_file_to_download == other.num_file_to_download &&
    self.num_byte_to_download == other.num_byte_to_download &&
    self.speed_download == other.speed_download &&
    self.num_file_to_upload == other.num_file_to_upload &&
    self.num_byte_to_upload == other.num_byte_to_upload &&
    self.speed_upload == other.speed_upload &&
    IsEqualBOOL(self.is_transfering, other.is_transfering);
}
@end


@implementation TreePairStatusObjc

- (id)initWithTreePairStatus:(const zs::TreePairStatus &)obj {
  self = [super init];
  if (self) {
    _local_tree_id = obj.local_tree_id;
    _remote_tree_id = obj.remote_tree_id;
    _static_num_file_to_download = obj.static_num_file_to_download;
    _static_num_file_to_upload = obj.static_num_file_to_upload;
    _static_num_byte_to_download = obj.static_num_byte_to_download;
    _static_num_byte_to_upload = obj.static_num_byte_to_upload;
    _static_num_file_consistent = obj.static_num_file_consistent;
    _static_num_byte_consistent = obj.static_num_byte_consistent;
    
    _num_file_to_download = obj.num_file_to_download;
    _num_file_to_upload = obj.num_file_to_upload;
    _num_byte_to_download = obj.num_byte_to_download;
    _num_byte_to_upload = obj.num_byte_to_upload;
    _speed_download = obj.speed_download;
    _speed_upload = obj.speed_upload;
    _download_file = [NSString stringWithUTF8String:obj.download_file.c_str()];
    _upload_file = [NSString stringWithUTF8String:obj.upload_file.c_str()];
    _is_transfering = obj.is_transfering;
  }
  return self;
}

- (BOOL)isEqual:(id)object {
  if (self == object) {
    return YES;
  }
  if (!object || ![object isKindOfClass:[TreePairStatusObjc class]]) {
    return NO;
  }
  
  TreePairStatusObjc *other = (TreePairStatusObjc*)object;
  return
    self.local_tree_id == other.local_tree_id &&
    self.remote_tree_id == other.remote_tree_id &&
    self.static_num_file_to_download == other.static_num_file_to_download &&
    self.static_num_file_to_upload == other.static_num_file_to_upload &&
    self.static_num_byte_to_download == other.static_num_byte_to_download &&
    self.static_num_byte_to_upload == other.static_num_byte_to_upload &&
    self.static_num_file_consistent == other.static_num_file_consistent &&
    self.static_num_byte_consistent == other.static_num_byte_consistent &&
  
    self.num_file_to_download == other.num_file_to_download &&
    self.num_file_to_upload == other.num_file_to_upload &&
    self.num_byte_to_download == other.num_byte_to_download &&
    self.num_byte_to_upload == other.num_byte_to_upload &&
    self.speed_download == other.speed_download &&
    self.speed_upload == other.speed_upload &&
    [self.download_file isEqualToString:other.download_file] &&
    [self.upload_file isEqualToString:other.upload_file] &&
    IsEqualBOOL(self.is_transfering, other.is_transfering);
}

@end


@implementation FileTransferStatObjc

- (id)initWithFileTransferStat:(const zs::FileTransferStat &)obj {
  self = [super init];
  if (self) {
    _local_path = [NSString stringWithUTF8String:obj.local_path.c_str()];
    _remote_path = [NSString stringWithUTF8String:obj.remote_path.c_str()];
    _bytes_file_size = obj.bytes_file_size;
    _bytes_to_transfer = obj.bytes_to_transfer;
    _speed = obj.speed;
    _transfer_status = obj.transfer_status;
  }
  return self;
}

- (BOOL)isEqual:(id)object {
  if (self == object) {
    return YES;
  }
  if (!object || ![object isKindOfClass:[FileTransferStatObjc class]]) {
    return NO;
  }
  
  FileTransferStatObjc *other = (FileTransferStatObjc*)object;
  return
    [self.local_path isEqualToString:other.local_path] &&
    [self.remote_path isEqualToString:other.remote_path] &&
    self.bytes_file_size == other.bytes_file_size &&
    self.bytes_to_transfer == other.bytes_to_transfer &&
    self.speed == other.speed &&
    self.transfer_status == other.transfer_status;
}
@end


@implementation TransferListStatusObjc

- (id)initWithTransferListStatus:(const zs::TransferListStatus &)obj
                          treeId:(int32_t)tree_id_{
  self = [super init];
  if (self) {
    NSMutableArray *temp = [NSMutableArray arrayWithCapacity:obj.list_.size()];
    for(auto it = obj.list_.begin(); it != obj.list_.end(); ++it) {
      [temp addObject:[[FileTransferStatObjc alloc] initWithFileTransferStat:*it]];
    }
    _list_ = temp;
    _tree_id = tree_id_;
  }
  return self;
}

@end


@implementation IpPortObjc

- (id)initWithIpPort:(const zs::IpPort &)ipport {
  self = [super init];
  if (self) {
    _ip = [NSString stringWithUTF8String:ipport.ip_.c_str()];
    _port = ipport.port_;
  }
  return self;
}

@end


@implementation ListStaticPeersObjc

- (id)initWithListStaticPeers:(const zs::ListStaticPeers &)listStaticPeers {
  self = [super init];
  if (self) {
    NSMutableArray *temp = [NSMutableArray
                            arrayWithCapacity:listStaticPeers.peers.size()];
    for(auto it = listStaticPeers.peers.begin()
        ; it != listStaticPeers.peers.end()
        ; ++it) {
      [temp addObject:[[IpPortObjc alloc] initWithIpPort:*it]];
    }
    _peers = temp;
  }
  return self;
}
@end

@implementation DownloadStatusObjc

- (id)initWithDownloadStatus:(const zs::DownloadStatus &)downloadStatus {
  self = [super init];
  if (self) {
    _status = downloadStatus.status;
    _total_num_byte = downloadStatus.total_num_byte;
    _num_byte_to_download = downloadStatus.num_byte_to_download;
    _speed_download = downloadStatus.speed_download;
    _error_code = downloadStatus.error_code;
  }
  return self;
}

@end

@implementation UploadStatusObjc

- (id)initWithUploadStatus:(const zs::UploadStatus &)uploadStatus {
  self = [super init];
  if (self) {
    _status = uploadStatus.status;
    _total_num_byte = uploadStatus.total_num_byte;
    _num_byte_to_upload = uploadStatus.num_byte_to_upload;
    _speed_upload = uploadStatus.speed_upload;
    _error_code = uploadStatus.error_code;
  }
  return self;
}

@end
