#include <memory>
#include <string.h>
#include <assert.h>
#include <string>
#include <algorithm>
#include "zisync/kernel/tree_status.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/content_provider.h"
#include "zisync/kernel/format.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/platform/platform.h"

#ifdef __APPLE__
#import <Foundation/Foundation.h>
#include "zisync/kernel/notification/notification.h"
#include "zisync/kernel/kernel_stats.h"
#endif

namespace zs {
using std::vector;
using std::list;

TreeManager TreeManager::s_instance_;

ITreeManager* GetTreeManager() {
  return &TreeManager::s_instance_;
}

void TreeStat::OnFileIndexed(int32_t count) {
  file_to_index_.FetchAndSub(count);
  PostNotification();
}

void TreeStat::OnFileTransfered(StatusType type, int32_t count) {
  if (type == ST_PUT) {
    file_to_upload_.FetchAndSub(count);
  } else if (type == ST_GET) {
    file_to_download_.FetchAndSub(count);
  }
  PostNotification();
}

void TreeStat::OnFileSkiped(StatusType type, int32_t count) {
  if (type == ST_PUT) {
    file_to_upload_.FetchAndSub(count);
  } else if (type == ST_GET) {
    file_to_download_.FetchAndSub(count);
  }
  PostNotification();
}

void TreeStat::OnByteTransfered(StatusType type, int64_t nbytes) {
  if (type == ST_PUT) {
    byte_to_upload_.FetchAndSub(nbytes);
    byte_upload_time_unit_.FetchAndInc(nbytes);
  } else if (type == ST_GET) {
    byte_to_download_.FetchAndSub(nbytes);
    byte_download_time_unit_.FetchAndInc(nbytes);
  }
  PostNotification();
}

void TreeStat::OnByteSkiped(StatusType type, int64_t nbytes) {
  if (type == ST_PUT) {
    byte_to_upload_.FetchAndSub(nbytes);
  } else if (type == ST_GET) {
    byte_to_download_.FetchAndSub(nbytes);
  }
  PostNotification();
}

void TreeStat::OnTreeStatBegin(StatusType type,
                               int32_t total_files,
                               int64_t total_bytes) {
  if (type == ST_PUT) {
    file_to_upload_.FetchAndInc(total_files);
    byte_to_upload_.FetchAndInc(total_bytes);
  } else if (type == ST_GET) {
    file_to_download_.FetchAndInc(total_files);
    byte_to_download_.FetchAndInc(total_bytes);
  } else if (type == ST_INDEX) {
    file_to_index_.FetchAndInc(total_files);
  }
  is_transfering_ = true;
}

void TreeStat::OnTreeStatEnd(StatusType type,
                             int32_t left_files,
                             int64_t left_bytes) {
  if (type == ST_PUT) {
    file_to_upload_.FetchAndSub(left_files);
    byte_to_upload_.FetchAndSub(left_bytes);
  } else if (type == ST_GET) {
    file_to_download_.FetchAndSub(left_files);
    byte_to_download_.FetchAndSub(left_bytes);
  } else if (type == ST_INDEX) {
    file_to_index_.FetchAndSub(left_files);
  }

  is_transfering_ = false;
  PostNotification();
}

void TreeStat::QueryTreeStatus(TreeStatus *status) {
  assert(status != NULL);
  status->num_file_to_index = file_to_index_.value();
  status->num_byte_to_upload = byte_to_upload_.value();
  status->num_file_to_upload = file_to_upload_.value();
  status->speed_upload = upload_speed_.value();
  status->num_file_to_download = file_to_download_.value();
  status->num_byte_to_download = byte_to_download_.value();
  status->speed_download = download_speed_.value();
  status->is_transfering = is_transfering_;
  status->file_indexing = file_indexing_;
}
  
void TreeStat::CalcSpeed() {
  upload_speed_.set_value(byte_upload_time_unit_.value());
  download_speed_.set_value(byte_download_time_unit_.value());
  byte_upload_time_unit_.set_value(0);
  byte_download_time_unit_.set_value(0);

  /* tree stat log */
  std::string s, size, speed, lefttime;
  if (file_to_index_.value() != 0) {
    StringFormat(&s, "Indexing %d files", file_to_index_.value());
  }

  if (file_to_upload_.value() != 0) {
    if (s.size() > 0) {
      s.append(1, ',');
    }
    StringAppendFormat(&s, "uploading %d files, left %s bytes",
                       file_to_upload_.value(),
                       HumanFileSize(byte_to_upload_.value(), &size));
    if (upload_speed_.value() != 0) {
      int seconds = static_cast<int>(byte_to_upload_.value() / upload_speed_.value());
      StringAppendFormat(&s, "[ %s, etc: %s ]",
                         HumanSpeed(upload_speed_.value(), &speed),
                         HumanTime(seconds, &lefttime));
    }
  }

  if (file_to_download_.value() != 0) {
    if (s.size() > 0) {
      s.append(1, ',');
    }
    StringAppendFormat(&s, "downloading %d files, left %s bytes",
                       file_to_download_.value(),
                       HumanFileSize(byte_to_download_.value(), &size));
    if (download_speed_.value() != 0) {
      int seconds = static_cast<int>(
          byte_to_download_.value() / download_speed_.value());
      StringAppendFormat(
          &s, "[ %s, etc: %s ]", HumanSpeed(download_speed_.value(), &speed),
          HumanTime(seconds, &lefttime));
    }
  }

  if (s.size() > 0) {
    ZSLOG_INFO("%s", s.c_str());
  }
}

void TreeStat::ClearSpeed() {
  download_speed_.set_value(0);
  upload_speed_.set_value(0);
}

void TreeStat::OnFileWillIndex(const std::string &file_indexing) {
  MutexAuto mutex_auto(&mutex_);
  file_indexing_ = file_indexing;
  PostNotification();
}

void TreeStat::Reset(TreeStatus *status) {
  status->num_file_to_index = 0;
  status->num_file_to_download = 0;
  status->num_byte_to_download = 0;
  status->speed_download = 0;
  status->num_file_to_upload = 0;
  status->num_byte_to_upload = 0;
  status->speed_upload = 0;
  status->is_transfering = false;
  status->file_indexing.clear();
}

  void TreeStat::PostNotification() {
#ifdef __APPLE__
    @autoreleasepool {
      
      TreeStatus status;
      status.tree_id = tree_id_;
      QueryTreeStatus(&status);
      TreeStatusObjc *treeStatus = [[TreeStatusObjc alloc] initWithTreeStatus:
                                    status];
      NSDictionary *useInfo = @{kZSNotificationUserInfoData:treeStatus};
      zs::DefaultNotificationCenter()->PostNotificationAtInterval(ZSNotificationNameUpdateTreeStatus
                                                        , (void*)CFBridgingRetain(useInfo), 500);
    }
#endif
  }
  
void TreePairStat::OnFileTransfer(StatusType type, const string &path) {
  MutexAuto mutex_auto(&mutex_);
  if (type == ST_PUT) {
    upload_file_ = path;
  } else if (type == ST_GET) {
    download_file_ = path;
  }
  PostNotification();
}

void TreePairStat::OnFileTransfered(StatusType type, int32_t count) {
  MutexAuto mutex_auto(&mutex_);
  if (type == ST_PUT) {
    file_to_upload_ -= count;
  } else if (type == ST_GET) {
    file_to_download_ -= count;
  }
  PostNotification();
}

void TreePairStat::OnFileSkiped(StatusType type, int32_t count) {
  MutexAuto mutex_auto(&mutex_);
  if (type == ST_PUT) {
    file_to_upload_ -= count;
  } else if (type == ST_GET) {
    file_to_download_ -= count;
  }
  PostNotification();
}

void TreePairStat::OnByteTransfered(StatusType type, int64_t nbytes) {
  MutexAuto mutex_auto(&mutex_);
  if (type == ST_PUT) {
    byte_to_upload_ -= nbytes;
    byte_upload_time_unit_ += nbytes;
  } else if (type == ST_GET) {
    byte_to_download_ -= nbytes;
    byte_download_time_unit_ += nbytes;
  }
  PostNotification();
}

void TreePairStat::OnByteSkiped(StatusType type, int64_t nbytes) {
  if (type == ST_PUT) {
    byte_to_upload_ -= nbytes;
  } else if (type == ST_GET) {
    byte_to_download_ -= nbytes;
  }
  PostNotification();
}

void TreePairStat::OnTreeStatBegin(StatusType type,
                                   int32_t total_files,
                                   int64_t total_bytes) {
  MutexAuto mutex_auto(&mutex_);
  if (type == ST_PUT) {
    file_to_upload_ += total_files;
    byte_to_upload_ += total_bytes;
  } else if (type == ST_GET) {
    file_to_download_ += total_files;
    byte_to_download_ += total_bytes;
  } 
  is_transfering_ = true;
  PostNotification();
}

void TreePairStat::OnTreeStatEnd(StatusType type,
                                 int32_t left_files,
                                 int64_t left_bytes) {
  MutexAuto mutex_auto(&mutex_);
  if (type == ST_PUT) {
    file_to_upload_ -= left_files;
    byte_to_upload_ -= left_bytes;
    upload_file_.clear();
  } else if (type == ST_GET) {
    file_to_download_ -= left_files;
    byte_to_download_ -= left_bytes;
    download_file_.clear();
  }
  is_transfering_ = false;
  PostNotification();
}

void TreePairStat::QueryTreePairStatus(TreePairStatus *status) {
  MutexAuto mutex_auto(&mutex_);
  QueryTreePairStatusNoLock(status);
}

  void TreePairStat::QueryTreePairStatusNoLock(zs::TreePairStatus *status) {
    
    assert(status != NULL);
    status->num_byte_to_upload = byte_to_upload_;
    status->num_file_to_upload = file_to_upload_;
    status->num_file_to_download = file_to_download_;
    status->num_byte_to_download = byte_to_download_;
    status->speed_upload = upload_speed_;
    status->speed_download = download_speed_;
    status->is_transfering = is_transfering_;
    
    status->static_num_file_to_upload = static_file_to_upload_;
    status->static_num_file_to_download = static_file_to_download_;
    status->static_num_file_consistent = static_file_consistent_;
    status->static_num_byte_to_upload = static_byte_to_upload_;
    status->static_num_byte_to_download = static_byte_to_download_;
    status->static_num_byte_consistent = static_byte_consistent_;
    
    status->upload_file = upload_file_;
    status->download_file = download_file_;
  }
  
void TreePairStat::CalcSpeed() {
  MutexAuto mutex_auto(&mutex_);
  upload_speed_ = byte_upload_time_unit_;
  download_speed_ = byte_download_time_unit_;
  byte_upload_time_unit_ = 0;
  byte_download_time_unit_ = 0;
}

void TreePairStat::ClearSpeed() {
  download_speed_ = 0;
  upload_speed_ = 0;
}
  
  void TreePairStat::PostNotification() {
#ifdef __APPLE__
    @autoreleasepool {
      
      TreePairStatus status;
      status.local_tree_id = local_tree_id_;
      status.remote_tree_id = remote_tree_id_;
      QueryTreePairStatusNoLock(&status);
      TreePairStatusObjc *statusObjs = [[TreePairStatusObjc alloc] initWithTreePairStatus:status];
      NSDictionary *userInfo = @{kZSNotificationUserInfoData:statusObjs};
      zs::DefaultNotificationCenter()->PostNotificationAtInterval(ZSNotificationNameUpdateTreePairStatus, (void*)CFBridgingRetain(userInfo), 500);
    }
#endif
  }
  
void TransferList::ConstructMe() {
  int ret = mutex_.Initialize();
  tree_id_ = -1;
  assert(ret == 0);
}

TransferList::~TransferList() {
  int ret = mutex_.CleanUp();
  assert(ret == 0);
}

void TransferList::OnFileTransfered(StatusType type,
                                    int64_t id,
                                    const std::string &encode_path) {
  MutexAuto auto_mutex(&mutex_);
  auto it = file_sort_.find(id);
  if (it == file_sort_.end()) {
    ZSLOG_INFO("Tree has transfer end!.");
    return;
  }

  auto file_it = it->second.find(encode_path);
  if (file_it == it->second.end()) {
    ZSLOG_ERROR("Transfer list has no file: %s", encode_path.c_str());
    return;
  } else {
    it->second.erase(file_it);
  }
  
  PostNotification();
}

void TransferList::OnFileSkiped(StatusType type,
                                int64_t id,
                                const std::string &encode_path) {
  MutexAuto auto_mutex(&mutex_);
  auto it = file_sort_.find(id);
  if (it == file_sort_.end()) {
    ZSLOG_INFO("Tree has transfer end!.");
    return;
  }

  auto file_it = it->second.find(encode_path);
  if (file_it == it->second.end()) {
    ZSLOG_ERROR("Transfer list has no file: %s", encode_path.c_str());
    return;
  } else {
    it->second.erase(file_it);
  }
  
  PostNotification();
}
void TransferList::OnByteTransfered(StatusType type,
                                    int64_t nbytes,
                                    int64_t id,
                                    const std::string &encode_path) {
  MutexAuto auto_mutex(&mutex_);
  auto it = file_sort_.find(id);
  if (it == file_sort_.end()) {
    //ZSLOG_INFO("Tree has transfer end!");
    return;
  }

  auto stat = it->second.find(encode_path);
  if (stat != it->second.end()) {
    stat->second.type = type;
    stat->second.bytes_to_transfer -= nbytes;
  } else {
    //ZSLOG_INFO("File(%s) has been transfered.", encode_path.c_str());
  }

  auto speed = speeds_.find(id);
  if (speed == speeds_.end()) {
    //ZSLOG_ERROR("Speed not recorded of %" PRId64, id);
    return;
  } else {
    speed->second.byte_to_transfer_time_unit += nbytes;
  }
  
  PostNotification();
}

void TransferList::OnByteSkiped(StatusType type,
                                int64_t nbytes,
                                int64_t id,
                                const std::string &encode_path) {
  MutexAuto auto_mutex(&mutex_);
  auto it = file_sort_.find(id);
  if (it == file_sort_.end()) {
    ZSLOG_INFO("Tree has transfer end!");
    return;
  }

  auto stat = it->second.find(encode_path);
  if (stat != it->second.end()) {
    stat->second.bytes_to_transfer -= nbytes;
  } else {
    ZSLOG_INFO("File(%s) has been skiped.", encode_path.c_str());
  }
  
  PostNotification();
}

void TransferList::AppendFile(const std::string &local_path,
                              const std::string &remote_path,
                              const std::string &encode_path,
                              int64_t length,
                              int64_t id) {
  auto type = types_.find(id);
  if (type == types_.end()) {
    ZSLOG_ERROR("Has no record of %" PRId64, id);
    return;
  }

  auto it = file_sort_.find(id);
  if (it == file_sort_.end()) {
    ZSLOG_ERROR("Has no record of %" PRId64, id);
    return;
  }

  FilePairStat fs = {local_path, remote_path, length, length, type->second};
  it->second[encode_path] = fs;
  
  PostNotification();
}

void TransferList::OnTransferListBegin(StatusType type,
                                       int64_t id) {
  MutexAuto auto_mutex(&mutex_);
  file_sort_[id] = std::map<std::string, FilePairStat>();
  if (type == ST_PUT) {
    types_[id] = ST_WAIT_PUT;
  } else if (type == ST_GET) {
    types_[id] = ST_WAIT_GET;
  }

  FileSpeed speed = {0, 0};
  speeds_[id] = speed;

  ids_.push_back(id);
}

void TransferList::OnTransferListEnd(StatusType type,
                                     int64_t id) {
  MutexAuto auto_mutex(&mutex_);
  {
    auto it = file_sort_.find(id);
    if (it == file_sort_.end()) {
      ZSLOG_ERROR("Has no record of %" PRId64, id);
    } else {
      it->second.clear();
      file_sort_.erase(it);
    }
  }

  {
    auto it = types_.find(id);
    if (it == types_.end()) {
      ZSLOG_ERROR("Has no record of %" PRId64, id);
    } else {
      types_.erase(it);
    }
  }

  {
    auto it = speeds_.find(id);
    if (it == speeds_.end()) {
      ZSLOG_ERROR("Has no record of %" PRId64, id);
      assert(0);
    } else {
      speeds_.erase(it);
    }
  }

  {
    auto it = std::find(ids_.begin(), ids_.end(), id);
    if (it == ids_.end()) {
      ZSLOG_ERROR("Has no record of %" PRId64, id);
      assert(0);
    } else {
      ids_.erase(it);
    }
  }
  
  PostNotification();
}

bool TransferList::IsActive(const FilePairStat &stat) {
  return stat.type == ST_PUT ||
      stat.type== ST_GET;
}

void TransferList::InitFileTransferStat(
    FileTransferStat *stat, const FilePairStat &fp) {
  stat->local_path = fp.local_path;
  stat->remote_path = fp.remote_path;
  stat->bytes_file_size = fp.length;
  stat->bytes_to_transfer = fp.bytes_to_transfer;
  stat->speed = 0;
  switch (fp.type) {
    case ST_PUT:
      stat->transfer_status = FILE_TRANSFER_STATUS_UP;
      break;
    case ST_GET:
      stat->transfer_status = FILE_TRANSFER_STATUS_DOWN;
      break;
    case ST_INDEX:
      stat->transfer_status = FILE_TRANSFER_STATUS_INDEX;
      break;
    case ST_WAIT_PUT:
      stat->transfer_status = FILE_TRANSFER_STATUS_WAITUP;
      break;
    case ST_WAIT_GET:
      stat->transfer_status = FILE_TRANSFER_STATUS_WAITDOWN;
      break;
    default:
      assert(false);
  }
}

void TransferList::CalcSpeed() {
  MutexAuto auto_mutex(&mutex_);
  for (auto it = speeds_.begin(); it != speeds_.end(); it++) {
    it->second.speed = it->second.byte_to_transfer_time_unit;
    it->second.byte_to_transfer_time_unit = 0;
  }
  // test log

  for (auto it = ids_.begin(); it != ids_.end(); it++) {
    auto find = file_sort_.find(*it);
    if (find == file_sort_.end()) {
      ZSLOG_ERROR("Transfer list records is not disunity!");
      continue;
    }

    auto s = speeds_.find(*it);
    if (s == speeds_.end()) {
      ZSLOG_ERROR("Record is not unite");
      continue;
    }
  
    for (auto file = find->second.begin(); file != find->second.end();
         file++) {
      if (IsActive(file->second)) {
        std::string info;
        std::string speed;
        if (file->second.type == ST_PUT) {
          StringAppendFormat(&info, "uploading file %s ==> %s",
                             file->second.local_path.c_str(),
                             file->second.remote_path.c_str());
        } else if (file->second.type == ST_GET) {
          StringAppendFormat(&info, "downloading file %s <== %s",
                             file->second.local_path.c_str(),
                             file->second.remote_path.c_str());
        }
        StringAppendFormat(&info, ", left %s bytes[%s]",
                           HumanFileSize(file->second.bytes_to_transfer),
                           HumanSpeed(s->second.speed, &speed));
        //ZSLOG_INFO("%s", info.c_str());
      }
    }
  }
}

void TransferList::QueryTransferList(TransferListStatus *list,
                                     int32_t offset,
                                     int32_t max_num) {
  MutexAuto auto_mutex(&mutex_);
  QueryTransferListNoLock(list, offset, max_num);
}
  
  void TransferList::QueryTransferListNoLock(TransferListStatus *list, int32_t offset, int32_t max_num) {
    list->list_.clear();
    if(max_num == 0) {
      return;
    }
    assert(max_num == -1 || max_num > 0);
    assert(offset >= 0);
    CollectActiveFile(list, max_num);
    CollectStaticFile(list, offset, max_num - list->list_.size());
    list->list_.shrink_to_fit();
  }

void TransferList::CollectActiveFile(
    TransferListStatus *list, int32_t max_num) {
  if (max_num == 0) {
    return;
  }
  for (auto id= ids_.begin(); id!= ids_.end(); id++) {
    auto sort = file_sort_.find(*id);
    if (sort == file_sort_.end()) {
      ZSLOG_ERROR("Record is not unite.");
      continue;
    }
    for (auto file = sort->second.begin();
         file != sort->second.end() && max_num != 0; file++) {
      if (IsActive(file->second)) {
        FileTransferStat fts;
        InitFileTransferStat(&fts, file->second);
        auto speed = speeds_.find(*id);
        if (speed == speeds_.end()) {
           ZSLOG_ERROR("Record is not unite.");
           assert(0);
        }
        fts.speed = speed->second.speed;
        list->list_.push_back(fts);
        max_num--;
        break;
      }
    }
  }
}

void TransferList::CollectStaticFile(
    TransferListStatus *list, int32_t offset, int32_t max_num) {
  if (max_num == 0) {
    return;
  }

  for (auto id = ids_.begin(); id != ids_.end(); id++) {
    auto sort = file_sort_.find(*id);
    if (sort == file_sort_.end()) {
      ZSLOG_ERROR("Record is not unite.");
      continue;
    }
    if (offset > static_cast<int32_t>(sort->second.size())) {
      offset -= sort->second.size();
      continue;
    }

    for (auto file = sort->second.begin();
         file != sort->second.end() && max_num != 0; file++) {
      if (offset > 0) {
        offset--;
        continue;
      }
      if (!IsActive(file->second)) {
        FileTransferStat fts;
        InitFileTransferStat(&fts, file->second);
        list->list_.push_back(fts);
        max_num--;
      }
    }
  }
}
  
  void TransferList::PostNotification() {
#ifdef __APPLE__
    @autoreleasepool {
      
      TransferListStatus status;
      QueryTransferListNoLock(&status, 0, -1);
      TransferListStatusObjc *transferList = [[TransferListStatusObjc alloc] initWithTransferListStatus:status
                                                                                                 treeId:tree_id_];
      NSDictionary *userInfo = @{kZSNotificationUserInfoData : transferList};
      zs::DefaultNotificationCenter()->PostNotificationAtInterval(ZSNotificationNameUpdateTransferListStatus, (void*)CFBridgingRetain(userInfo), 500);
    }
#endif
  }

TreeManager::TreeManager()
  : reference_count_(0), timer_(NULL) {
    int ret = mutex_.Initialize();
    assert(ret == 0);
    ret = timer_mutex_.Initialize();
    assert(ret == 0);
  }

TreeManager::~TreeManager() {
  assert(reference_count_  == 0);
  status_.clear();
  mutex_.CleanUp();
  assert(timer_ == NULL);
}

err_t TreeManager::StartupTimer() {
  MutexAuto auto_mutex(&timer_mutex_);
  if (reference_count_ == 0) {
    timer_ = new OsTimer(1000, this);

    if (timer_->Initialize() != 0) {
      ZSLOG_ERROR("Timer init fail: %s", OsGetLastErr());
      return ZISYNC_ERROR_OS_TIMER;
    }
  }
  reference_count_ ++;
  return ZISYNC_SUCCESS;
}

err_t TreeManager::ShutdownTimer() {
  err_t ret = ZISYNC_SUCCESS;
  MutexAuto auto_mutex(&timer_mutex_);
  reference_count_ --;
  if (reference_count_ == 0) {
    if (timer_) {
      if (timer_->CleanUp() != 0) {
        ZSLOG_ERROR("Timer cleanup fail: %s", OsGetLastErr());
        ret = ZISYNC_ERROR_OS_TIMER;
      }
      delete timer_;
      timer_ = NULL;
    }

    {
      MutexAuto auto_mutex(&mutex_);
      for (auto it = status_.begin(); it != status_.end(); it++) {
        it->second->ClearSpeed();
      }
      for (auto it = pair_status_.begin(); it != pair_status_.end(); it++) {
        it->second->ClearSpeed();
      }
    }
  }

  return ret;
}

void TreeManager::OnTaskBegin(
    int32_t local_tree_id, int32_t remote_tree_id, 
    StatusType type, int32_t total_files,
    int64_t total_bytes, int64_t id) {
  std::shared_ptr<ITreeStat> stat_ptr = GetTreeStat(local_tree_id);
  stat_ptr->OnTreeStatBegin(type, total_files, total_bytes);
  if (type != ST_INDEX) {
    std::shared_ptr<ITreePairStat> pair_stat_ptr = GetTreePairStat(
        local_tree_id, remote_tree_id);
    pair_stat_ptr->OnTreeStatBegin(type, total_files, total_bytes);
  }

  // TransferList
  std::shared_ptr<ITransferList> transfer_list_stat =
      GetTransferListStat(local_tree_id);
  transfer_list_stat->OnTransferListBegin(type, id);

  err_t ret = StartupTimer();
  assert(ret == ZISYNC_SUCCESS);
}

std::shared_ptr<ITreeStat> TreeManager::GetTreeStat(int32_t tree_id) {
  MutexAuto auto_mutex(&mutex_);
  std::shared_ptr<TreeStat> ptr;
  auto it = status_.find(tree_id);
  if (it == status_.end()) {
    ptr.reset(new TreeStat(tree_id));
    status_[tree_id] = ptr;
  } else {
    ptr = it->second;
  }
  return ptr;
}

std::shared_ptr<ITreePairStat> TreeManager::GetTreePairStat(
    int32_t local_tree_id, int32_t remote_tree_id) {
  MutexAuto auto_mutex(&mutex_);
  std::shared_ptr<TreePairStat> ptr;
  auto it = pair_status_.find(std::make_pair(local_tree_id, remote_tree_id));
  if (it == pair_status_.end()) {
    ptr.reset(new TreePairStat(local_tree_id, remote_tree_id));
    pair_status_[std::make_pair(local_tree_id, remote_tree_id)] = ptr;
  } else {
    ptr = it->second;
  }
  return ptr;
}

std::shared_ptr<ITransferList> TreeManager::GetTransferListStat(
    int32_t tree_id) {
  MutexAuto auto_mutex(&mutex_);
  std::shared_ptr<TransferList> ptr;
  auto it = transfer_list_stat_.find(tree_id);
  if (it == transfer_list_stat_.end()) {
    ptr.reset(new TransferList(tree_id));
    transfer_list_stat_[tree_id] = ptr;
  } else {
    ptr = it->second;
  }
  return ptr;
}

void TreeManager::OnTaskEnd(int32_t local_tree_id,
                            int32_t remote_tree_id,
                            StatusType type,
                            int32_t left_files,
                            int64_t left_bytes,
                            int64_t id) {
  //
  // NOTE: never call ShutdownTimer() while hoding mutex, since
  // OnTimer() will also aquaire mutex and deadlock
  //
  ShutdownTimer();

  // hoding mutex and handle taskend()
  {
    MutexAuto auto_mutex(&mutex_);
    auto stat_it = status_.find(local_tree_id);
    assert(stat_it != status_.end());
    stat_it->second->OnTreeStatEnd(type, left_files, left_bytes);
    if (type != ST_INDEX) {
      auto stat_it = pair_status_.find(std::make_pair(
              local_tree_id, remote_tree_id));
      assert(stat_it != pair_status_.end());
      stat_it->second->OnTreeStatEnd(type, left_files, left_bytes);
    }

    if (type != ST_INDEX) {
      auto transfer_list_it = transfer_list_stat_.find(local_tree_id);
      if (transfer_list_it == transfer_list_stat_.end()) {
        ZSLOG_ERROR("Has no tree(%d) records", local_tree_id);
      } else {
        transfer_list_it->second->OnTransferListEnd(type, id);
      }
    }
  }
}

err_t TreeManager::QueryTreeStatus(int32_t tree_id, TreeStatus *status) {
  assert(status != NULL);
  MutexAuto auto_mutex(&mutex_);
  status->tree_id = tree_id;
  if (tree_id == -1) {
    TreeStatus tmp_status;
    TreeStat::Reset(status);
    for (auto it = status_.begin(); it != status_.end(); it++) {
      it->second->QueryTreeStatus(&tmp_status);
      status->num_file_to_index += tmp_status.num_file_to_index;
      status->num_byte_to_upload += tmp_status.num_byte_to_upload;
      status->num_file_to_upload += tmp_status.num_file_to_upload;
      status->speed_upload += tmp_status.speed_upload;
      status->num_file_to_download += tmp_status.num_file_to_download;
      status->num_byte_to_download += tmp_status.num_byte_to_download;
      status->speed_download += tmp_status.speed_download;
      status->is_transfering |= tmp_status.is_transfering;
    }
  } else {
    auto it = status_.find(tree_id);
    if (it != status_.end()) {
      it->second->QueryTreeStatus(status);
    } else {
      status->num_file_to_index = 0; 
      status->num_byte_to_upload = 0; 
      status->num_file_to_upload = 0;
      status->speed_upload = 0;
      status->num_file_to_download = 0;
      status->num_byte_to_download = 0;
      status->speed_download = 0;
      status->is_transfering = false;
      status->file_indexing.clear();
      return ZISYNC_SUCCESS;
    }
  }

  return ZISYNC_SUCCESS;
}

err_t TreeManager::QueryTreePairStatus(
    int32_t local_tree_id, int32_t remote_tree_id, TreePairStatus *status) {
  assert(status != NULL);
  MutexAuto auto_mutex(&mutex_);
  status->local_tree_id = local_tree_id;
  status->remote_tree_id = remote_tree_id;
  auto it = pair_status_.find(std::make_pair(local_tree_id, remote_tree_id));
  if (it != pair_status_.end()) {
    it->second->QueryTreePairStatus(status);
  } else {
    status->num_byte_to_upload = 0; 
    status->num_file_to_upload = 0;
    status->num_file_to_download = 0;
    status->num_byte_to_download = 0;
    status->upload_file.clear();
    status->download_file.clear();
    status->is_transfering = false;
    status->static_num_file_to_download = 0;
    status->static_num_file_to_upload = 0;
    status->static_num_file_consistent = 0;
    status->static_num_byte_to_download = 0;
    status->static_num_byte_to_upload = 0;
    status->static_num_byte_consistent = 0;
  }

  return ZISYNC_SUCCESS;
}

err_t TreeManager::QueryTransferList(
    int32_t tree_id, TransferListStatus *list,
    int32_t offset, int32_t max_num) {
  MutexAuto auto_mutex(&mutex_);
  auto it = transfer_list_stat_.find(tree_id);
  if (it != transfer_list_stat_.end()) {
    it->second->QueryTransferList(list, offset, max_num);
  }

  return ZISYNC_SUCCESS;
}

void TreeManager::OnTimer() {
  MutexAuto auto_mutex(&mutex_);
  for (auto it = status_.begin(); it != status_.end(); it++) {
    it->second->CalcSpeed();
  }

  for (auto it = pair_status_.begin(); it != pair_status_.end(); it++) {
    it->second->CalcSpeed();
  }

  for (auto it = transfer_list_stat_.begin(); it != transfer_list_stat_.end();
       it++) {
    it->second->CalcSpeed();
  }
}

}  // namespace zs

