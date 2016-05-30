// Copyright 2015, zisync.com

#include <vector>
#include <algorithm>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/zmq.h"
#include "zisync/kernel/utils/query_cache.h"
#include "zisync/kernel/utils/context.h"
#include "zisync/kernel/utils/message.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/request.h"
#include "zisync/kernel/endpoint.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/utils/event_notifier.h"
#ifdef __APPLE__
#import <Foundation/Foundation.h>
#include "zisync/kernel/kernel_stats.h"
#include "zisync/kernel/notification/notification.h"
#endif

namespace zs {

using std::vector;
using std::unique_ptr;

const char query_cache_pull_uri[] = "inproc://query_cache_pull";
QueryCache QueryCache::query_cache;
static Mutex mutex;
static QuerySyncInfoResult sync_result;
static QueryBackupInfoResult backup_result;
#ifdef __APPLE__
  NSArray *lastSyncInfos = nil;
  NSArray *lastBackupInfos = nil;
#endif
static int64_t last_query_cache_update_time_in_ms = -1;
const int64_t QUERY_CACHE_UPDATE_INTERVAL_IN_MS = 500;
static bool has_query_cache_update_wait = true;

#ifdef __APPLE__
  //items must conform to ListItemChangeTypeInterface
  //items must be comparable using sortMethod
  static NSArray *GetDifferentInfos(NSArray *last
                                      , NSMutableArray *current
                                      , NSComparator sortMethod) {
    
    NSMutableArray *resultSyncInfos = [[NSMutableArray alloc] initWithCapacity:last.count + current.count];
    [current sortUsingComparator:sortMethod];
    
    for (NSUInteger i = 0, j = 0; i < last.count || j < current.count; ) {
      if (i == last.count) {
        assert(j < current.count);
        [(id<ListItemChangeTypeInterface>)current[j] setChangeType:ZSChangeTypeInsert];
        [resultSyncInfos addObject:current[j]];
        j++;
        continue;
      }
      
      if (j == current.count) {
        assert(i < last.count);
        [(id<ListItemChangeTypeInterface>)last[i] setChangeType:ZSChangeTypeDelete];
        [resultSyncInfos addObject:last[i]];
        i++;
        continue;
      }
      
      NSComparisonResult compRes = sortMethod(last[i], current[j]);
      switch (compRes) {
        case NSOrderedAscending:
          [(id<ListItemChangeTypeInterface>)last[i] setChangeType:ZSChangeTypeDelete];
          [resultSyncInfos addObject:last[i]];
          i++;
          break;
        case NSOrderedSame:
          if (![(id<ListItemChangeTypeInterface>)last[i] isEqual:current[j]]) {
            [(id<ListItemChangeTypeInterface>)current[j] setChangeType:ZSChangeTypeUpdate];
            [resultSyncInfos addObject:current[j]];
          }
          i++;
          j++;
          break;
        case NSOrderedDescending:
          [(id<ListItemChangeTypeInterface>)current[j] setChangeType:ZSChangeTypeInsert];
          [resultSyncInfos addObject:current[j]];
          j++;
          break;
        default:
          assert(false);
          break;
      }
    }
    return resultSyncInfos;
  }
  
#endif
  
class QueryCacheUpdateHandler : public MessageHandler {
 public:
  virtual ~QueryCacheUpdateHandler() {
  }
  //
  // @return google protobuf Message used for parse request.
  //
  virtual ::google::protobuf::Message* mutable_msg() {
    return NULL;
  }
  //
  // using @param userdata to pass need context for message handling.
  //
  virtual err_t HandleMessage(
      const ZmqSocket& socket, const MsgHead& head, void* userdata);
  void Handle();
};

static inline void QueryCacheUpdateIntern() {
  QuerySyncInfoResult sync_result_;
  QueryBackupInfoResult backup_result_;
  
  
  vector<unique_ptr<Sync>> syncs;
  Sync::QueryWhereStatusNormalTypeNotBackup(&syncs);
  for (auto iter = syncs.begin(); iter != syncs.end(); iter ++) {
    SyncInfo sync_info;
    bool success = (*iter)->ToSyncInfo(&sync_info);
    if (success && (
            sync_info.trees.size() != 0 ||
            sync_info.sync_perm == SYNC_PERM_DISCONNECT_UNRECOVERABLE)) {
      sync_result_.sync_infos.push_back(sync_info);
    }
  }
  
  backup_result_.backups.clear();
  vector<unique_ptr<Backup>> backups;
  Backup::QueryWhereStatusNormal(&backups);
  for (auto iter = backups.begin(); iter != backups.end(); iter ++) {
    BackupInfo backup_info;
    if ((*iter)->ToBackupInfo(&backup_info)) {
      backup_result_.backups.push_back(backup_info);
    }
  }

#ifdef __APPLE__
  NSArray *syncsUpdate = nil;
  NSArray *backupsUpdate = nil;
#endif
  
  {
    MutexAuto mutex_auto(&mutex);
    backup_result = backup_result_;
    sync_result = sync_result_;
#ifdef __APPLE__
    @autoreleasepool {
      NSMutableArray *currentSyncs = [NSMutableArray arrayWithCapacity:sync_result.sync_infos.size()];
      for (auto it = sync_result.sync_infos.begin(); it != sync_result.sync_infos.end(); ++it) {
        [currentSyncs addObject:[[SyncInfoObjc alloc] initWithSyncInfo:*it]];
      }
      syncsUpdate = GetDifferentInfos(lastSyncInfos, currentSyncs, [SyncInfoObjc comparatorForSortingWithSyncId]);
      lastSyncInfos = currentSyncs;
      
      NSMutableArray *currentBackups = [NSMutableArray arrayWithCapacity:backup_result.backups.size()];
      for (auto it = backup_result.backups.begin(); it != backup_result.backups.end(); ++it) {
        [currentBackups addObject:[[BackupInfoObjc alloc] initWithBackupInfo:*it]];
      }
      backupsUpdate = GetDifferentInfos(lastBackupInfos, currentBackups, [BackupInfoObjc comparatorForSortingWithBackupId]);
      lastBackupInfos = currentBackups;
      
      
      
      if (syncsUpdate.count > 0) {
        NSDictionary *userInfo = @{kZSNotificationUserInfoData:syncsUpdate};
        [[NSNotificationCenter defaultCenter] postNotificationName:@ZSNotificationNameUpdateSyncInfo
                                                            object:nil
                                                          userInfo:userInfo];
      }
      
      if (backupsUpdate.count > 0) {
        NSDictionary *userInfo = @{kZSNotificationUserInfoData:backupsUpdate};
        [[NSNotificationCenter defaultCenter] postNotificationName:@ZSNotificationNameUpdateBackupInfo
                                                            object:nil
                                                          userInfo:userInfo];
      }
      
    };
#endif
  }
  
}

err_t QueryCacheUpdateHandler::HandleMessage(
    const ZmqSocket& socket, const MsgHead& head, void* userdata) {
  QueryCache *query_cache = reinterpret_cast<QueryCache*>(userdata);
  query_cache->QueryCacheUpdate();
  return ZISYNC_SUCCESS;
}

class QueryCacheObserver : public ContentObserver{
 public:
  virtual ~QueryCacheObserver() {}
  //
  // Implement ContentObserver
  virtual void* OnQueryChange() { return NULL; }
  virtual void  OnHandleChange(void* lpChanges) {
    QueryCache::GetInstance()->NotifyModify();
  }
};

static QueryCacheObserver query_cache_observer;

QueryCache::QueryCache():
    OsThread("QueryCache"),
    pull(NULL), exit(NULL) {
      pull_handler_map_[MC_QUERY_CACHE_UPDATE_REQUEST] = 
          new QueryCacheUpdateHandler();
    }

QueryCache::~QueryCache() {
  for (auto iter = pull_handler_map_.begin(); 
       iter != pull_handler_map_.end(); iter ++) {
    delete iter->second;
  }
  if (pull != NULL) {
    delete pull;
    pull = NULL;
  }
  if (exit != NULL) {
    delete exit;
    exit = NULL;
  }
}


int QueryCache::Run() {
  while (1) {
    zmq_pollitem_t items[] = {
      { pull->socket(), 0, ZMQ_POLLIN, 0 },
      { exit->socket(), 0, ZMQ_POLLIN, 0 },
    };

    int64_t timeout_in_ms;
    if (has_query_cache_update_wait) {
      timeout_in_ms = last_query_cache_update_time_in_ms + 
          QUERY_CACHE_UPDATE_INTERVAL_IN_MS - OsTimeInMs();
      timeout_in_ms = timeout_in_ms < 0 ? 0 : timeout_in_ms;
    } else {
      timeout_in_ms = -1;
    }
    int ret = zmq_poll(
        items, sizeof(items) / sizeof(zmq_pollitem_t) , (long)timeout_in_ms);
    if (ret == -1) {
      continue;
    }

    if (items[0].revents & ZMQ_POLLIN) {
      MessageContainer container(pull_handler_map_, false);
      container.RecvAndHandleSingleMessage(*pull, this);
    } else {
      this->QueryCacheUpdate();
    }

    if (items[1].revents & ZMQ_POLLIN) {
      return 0;
    }
  }

  return 0;
}

err_t QueryCache::Startup() {
  err_t zisync_ret;
  if (pull == NULL) {
    pull = new ZmqSocket(GetGlobalContext(), ZMQ_PULL);
  }
  if (exit == NULL) {
    exit = new ZmqSocket(GetGlobalContext(), ZMQ_SUB);
  }
  zisync_ret = pull->Bind(query_cache_pull_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  zisync_ret = exit->Connect(exit_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);
  
  int ret = OsThread::Startup();
  if (ret == -1) {
    ZSLOG_ERROR("Start thread fail");
    return ZISYNC_ERROR_OS_THREAD;
  }
  
  QueryCacheUpdateIntern();
  IContentResolver *resolver = GetContentResolver();
  resolver->RegisterContentObserver(
      TableDevice::URI, false, &query_cache_observer); 
  resolver->RegisterContentObserver(
      TableSync::URI, false, &query_cache_observer); 
  resolver->RegisterContentObserver(
      TableTree::URI, false, &query_cache_observer); 
  resolver->RegisterContentObserver(
      TableShareSync::URI, false, &query_cache_observer); 
  

  return ZISYNC_SUCCESS;
}

void QueryCache::Shutdown() {
  IContentResolver *resolver = GetContentResolver();
  resolver->UnregisterContentObserver(TableDevice::URI, &query_cache_observer); 
  resolver->UnregisterContentObserver(TableSync::URI, &query_cache_observer); 
  resolver->UnregisterContentObserver(TableTree::URI, &query_cache_observer); 
  resolver->UnregisterContentObserver(TableShareSync::URI, &query_cache_observer); 
  OsThread::Shutdown();
  if (pull != NULL) {
    delete pull;
    pull = NULL;
  }
  if (exit != NULL) {
    delete exit;
    exit = NULL;
  }
}

err_t QueryCache::QuerySyncInfo(QuerySyncInfoResult *result) const {
  MutexAuto mutex_auto(&mutex);
  *result = sync_result;
  return ZISYNC_SUCCESS;
}

err_t QueryCache::QuerySyncInfo(int32_t sync_id, SyncInfo *sync_info) const {
  MutexAuto mutex_auto(&mutex);
  auto find = std::find_if(
      sync_result.sync_infos.begin(), sync_result.sync_infos.end(),
      [ sync_id ] (const SyncInfo &sync_info) 
      { return sync_info.sync_id == sync_id; });
  if (find != sync_result.sync_infos.end()) {
    *sync_info = *find;
    return ZISYNC_SUCCESS;
  } else {
    return ZISYNC_ERROR_SYNC_NOENT;
  }
}

err_t QueryCache::QueryBackupInfo(QueryBackupInfoResult *result) const {
  MutexAuto mutex_auto(&mutex);
  *result = backup_result;
  return ZISYNC_SUCCESS;
}

err_t QueryCache::QueryBackupInfo(
    int32_t backup_id, BackupInfo *backup_info) const {
  MutexAuto mutex_auto(&mutex);
  auto find = std::find_if(
      backup_result.backups.begin(), backup_result.backups.end(),
      [ backup_id ] (const BackupInfo &backup_info) 
      { return backup_info.backup_id == backup_id; });
  if (find != backup_result.backups.end()) {
    *backup_info = *find;
    return ZISYNC_SUCCESS;
  } else {
    return ZISYNC_ERROR_SYNC_NOENT;
  }
}

void QueryCache::NotifyModify() {
  ZmqSocket push(GetGlobalContext(), ZMQ_PUSH);
  err_t zisync_ret = push.Connect(query_cache_pull_uri);
  assert(zisync_ret == ZISYNC_SUCCESS);

  QueryCacheUpdateRequest request;
  zisync_ret = request.SendTo(push);
  assert(zisync_ret == ZISYNC_SUCCESS);
}

void QueryCache::QueryCacheUpdate() {
  int64_t cur_time = OsTimeInMs();
  if (last_query_cache_update_time_in_ms == -1 || 
      cur_time - last_query_cache_update_time_in_ms > 
      QUERY_CACHE_UPDATE_INTERVAL_IN_MS) {
    QueryCacheUpdateIntern();
    GetEventNotifier()->NotifySyncModify();
    GetEventNotifier()->NotifyBackupModify();
    last_query_cache_update_time_in_ms = cur_time;
    has_query_cache_update_wait = false;
  } else {
    has_query_cache_update_wait = true;
  }
}

}  // namespace zs
