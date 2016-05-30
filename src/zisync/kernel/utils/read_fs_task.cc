// Copyright 2014, zisync.com

#include <memory>
#include <algorithm>
#include <cstring>

#include "zisync/kernel/utils/read_fs_task.h"
#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/utils/usn.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/abort.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/ignore.h"
#include "zisync/kernel/history/history_manager.h"
#include "zisync/kernel/monitor/monitor.h"


namespace zs {
using std::unique_ptr;
//extern const int64_t REPORT_EVENTS_TO_WORKER_INTERVAL_IN_MS;

int FsVisitor::Visit(const OsFileStat &stat) {
  if (zs::IsAborted() || zs::AbortHasFsTreeDelete(tree_id_)) {
    return -1;
  }
  files_.push_back(unique_ptr<FileStat>(new FileStat(stat, tree_root_)));
  return 0;
}

bool FsVisitor::IsIgnored(const string &path) const {
  return IsIgnoreDir(path) || IsIgnoreFile(path);
}

void FsVisitor::sort() {
  std::sort(files_.begin(), files_.end(), 
            [] (const unique_ptr<FileStat> &stat1, 
                const unique_ptr<FileStat> &stat2) {
            return strcmp(stat1->path(), stat2->path()) < 0;
            });
}

const int32_t ReadFsTask::TYPE_RDONLY = 1;
const int32_t ReadFsTask::TYPE_BACKUP_DST = 2;
const int32_t ReadFsTask::TYPE_RDWR = 3;

ReadFsTask::ReadFsTask(
    const string &tree_uuid, const string &tree_root, 
    int32_t tree_id, int32_t type):tree_uuid_(tree_uuid), 
    authority_(TableFile::GenAuthority(tree_uuid.c_str())),
    tree_root_(tree_root), tree_id_(tree_id), uri_(TableFile::GenUri(tree_uuid.c_str())), 
    error(ZISYNC_SUCCESS), has_change(false), 
    type_(type) {
      FixTreeRoot(&tree_root_);
      backup_type_ = -1;
    }

void ReadFsTask::ApplyBatch() {
  const int OP_LIST_COUNT_UPPER = 1000;
  if (op_list.GetCount() >= OP_LIST_COUNT_UPPER) {
    IContentResolver *resolver = GetContentResolver();
    int affected_row_num = resolver->ApplyBatch(authority_.c_str(), &op_list);
    if (affected_row_num != op_list.GetCount()) {
      error = ZISYNC_ERROR_GENERAL;
    }
    op_list.Clear();
  }
}

void ReadFsTask::ApplyBatchTail() {
  IContentResolver *resolver = GetContentResolver();
  int affected_row_num = resolver->ApplyBatch(authority_.c_str(), &op_list);
  if (affected_row_num != op_list.GetCount()) {
    error = ZISYNC_ERROR_GENERAL;
  }
  op_list.Clear();
  ResolvePendingOperations();
}

const int MASK_FILE_UPDATE_TYPE      = 0001,
      MASK_FILE_UPDATE_STATUS        = 0002,
      MASK_FILE_UPDATE_MTIME         = 0004,
      MASK_FILE_UPDATE_LENGTH        = 0010,
      MASK_FILE_UPDATE_SHA1          = 0020,
      MASK_FILE_UPDATE_UNIX_ATTR     = 0040,
      MASK_FILE_UPDATE_WIN_ATTR      = 0100,
      MASK_FILE_UPDATE_ANDROID_ATTR  = 0200,
      MASK_FILE_UPDATE_ALIAS         = 0400;

err_t ReadFsTask::AddUpdateFileOp(
    unique_ptr<FileStat> pfile_in_fs, unique_ptr<FileStat> pfile_in_db) {
  FileStat &file_in_fs = *pfile_in_fs;
  FileStat &file_in_db = *pfile_in_db;
  assert(file_in_fs.status != TableFile::STATUS_REMOVE);
  int mask_file_update = 0;
  string sha1;
  // ZSLOG_INFO("Maybe Update File(%s)", file_in_fs.path());
  if (file_in_fs.type != file_in_db.type) {
    mask_file_update |= MASK_FILE_UPDATE_TYPE;
  } 
  if (file_in_db.status != file_in_fs.status) {
    mask_file_update |= MASK_FILE_UPDATE_STATUS;
  } 
  if (file_in_fs.type == zs::OS_FILE_TYPE_REG) {
    if (HasMtimeChanged(file_in_fs.mtime, file_in_db.mtime)) {
      mask_file_update |= MASK_FILE_UPDATE_MTIME;
    } 
    if (file_in_fs.length != file_in_db.length) {
      mask_file_update |= MASK_FILE_UPDATE_LENGTH;
    }
    if (HasAttrChanged(
            file_in_fs.platform_attr, file_in_db.platform_attr)) {
      switch (GetPlatform()) {
        case PLATFORM_WINDOWS:
          mask_file_update |= MASK_FILE_UPDATE_WIN_ATTR;
          break;
        case PLATFORM_ANDROID:
          mask_file_update |= MASK_FILE_UPDATE_ANDROID_ATTR;
          break;
        default:
          mask_file_update |= MASK_FILE_UPDATE_UNIX_ATTR;
          break;
      }
    }
  }

  if (file_in_fs.alias != file_in_db.alias) {
    mask_file_update |= MASK_FILE_UPDATE_ALIAS;
  }

  if (file_in_fs.type == zs::OS_FILE_TYPE_REG) {
      if ((file_in_db.type != zs::OS_FILE_TYPE_REG) ||
          (file_in_db.status == TableFile::STATUS_REMOVE) ||
          (file_in_fs.mtime != file_in_db.mtime) ||
          (file_in_fs.length != file_in_db.length)) {
              /*  we does not use file_in_fs.sha1, becasue the file_in_fs is const */
              err_t ret = FileSha1(tree_root_ + file_in_fs.path(), 
                                   file_in_fs.alias, &sha1);
              if (sha1 != file_in_db.sha1) {

                  mask_file_update |= MASK_FILE_UPDATE_SHA1;
              }
              if (ret != ZISYNC_SUCCESS) {
                  ZSLOG_ERROR("Caculate file sha1 fail : %s", zisync_strerror(ret));
                  return ZISYNC_ERROR_SHA1_FAIL;
              }else{
                  FileStat *pfile = const_cast<FileStat*>(&file_in_fs);
                  pfile->sha1 = sha1;
              }
          }

  }  // else if dir ,sha1 is '\0'

  if (mask_file_update != 0) {
    ZSLOG_INFO("Update File(%s)", file_in_fs.path());
    has_change = true;
    ContentOperation *cp = op_list.NewUpdate(
        uri_, "%s = %" PRId32 " AND %s = %" PRId64, 
        TableFile::COLUMN_ID, file_in_db.id,
        TableFile::COLUMN_USN, file_in_db.usn);
    cp->SetCondition(this, false);
    ContentValues *cv = cp->GetContentValues();
    if (type_ == TYPE_RDONLY) { // just update vclock 
      mask_file_update = 0;
    }
    if (mask_file_update & MASK_FILE_UPDATE_TYPE) {
      cv->Put(TableFile::COLUMN_TYPE, file_in_fs.type);
    }
    if (mask_file_update & MASK_FILE_UPDATE_STATUS) {
      cv->Put(TableFile::COLUMN_STATUS, file_in_fs.status);
    }
    if (mask_file_update & MASK_FILE_UPDATE_MTIME) {
      cv->Put(TableFile::COLUMN_MTIME, file_in_fs.mtime);
    }
    if (mask_file_update & MASK_FILE_UPDATE_LENGTH) {
      cv->Put(TableFile::COLUMN_LENGTH, file_in_fs.length);
    }
    if (mask_file_update & MASK_FILE_UPDATE_SHA1) {
      // copy sha1
      cv->Put(TableFile::COLUMN_SHA1, sha1.c_str(), true);
    }
    if (mask_file_update & MASK_FILE_UPDATE_UNIX_ATTR) {
      cv->Put(TableFile::COLUMN_UNIX_ATTR, file_in_fs.unix_attr);
    }
    if (mask_file_update & MASK_FILE_UPDATE_WIN_ATTR) {
      cv->Put(TableFile::COLUMN_WIN_ATTR, file_in_fs.win_attr);
    }
    if (mask_file_update & MASK_FILE_UPDATE_ANDROID_ATTR) {
      cv->Put(TableFile::COLUMN_ANDROID_ATTR, file_in_fs.android_attr);
    }
    if (type_ == TYPE_BACKUP_DST || type_ == TYPE_RDONLY) {
      cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, 0);
      cv->Put(TableFile::COLUMN_REMOTE_VCLOCK, NULL, 0);
    } else {
      cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, file_in_db.local_vclock + 1);
    }
    if (mask_file_update & MASK_FILE_UPDATE_ALIAS) {
      cv->Put(TableFile::COLUMN_ALIAS, file_in_fs.alias, true);
    }

  int64_t ts_ = OsTimeInS() - 1;
	file_in_fs.time_stamp = ts_;
    if (file_in_db.status == TableFile::STATUS_NORMAL) {
      AddUpdateOperation(std::move(pfile_in_fs));
    }else {
      AddPendingInsertOperation(std::move(pfile_in_fs));
    }
    cv->Put(TableFile::COLUMN_MODIFIER, Config::device_name(), true);
    cv->Put(TableFile::COLUMN_TIME_STAMP, ts_);

  }

  ApplyBatch();
  
  return ZISYNC_SUCCESS;
}

err_t ReadFsTask::AddInsertFileOp(unique_ptr<FileStat> pfile_in_fs) {//hand over
  FileStat &file_in_fs = *pfile_in_fs;
  if (type_ == TYPE_RDONLY) {
    return ZISYNC_ERROR_GENERAL;
  }
  string sha1 = pfile_in_fs->sha1;
  if (file_in_fs.type == zs::OS_FILE_TYPE_REG
      && file_in_fs.sha1.empty()) {
    err_t zisync_ret = FileSha1(tree_root_ + file_in_fs.path(), 
                                file_in_fs.alias, &sha1);
    if (zisync_ret != ZISYNC_SUCCESS) {
      ZSLOG_ERROR("FileSha1(%s) fail : %s", 
                  (tree_root_ + file_in_fs.path()).c_str(), 
                  zisync_strerror(zisync_ret));
      return ZISYNC_ERROR_SHA1_FAIL;
    }else{
      file_in_fs.sha1 = sha1;
    }
  }
  ZSLOG_INFO("Insert File(%s)", file_in_fs.path());
  has_change = true;
  ContentOperation *cp = op_list.NewInsert(uri_, AOC_IGNORE);
  cp->SetCondition(this, false);
  ContentValues *cv = cp->GetContentValues();
  cv->Put(TableFile::COLUMN_TYPE, file_in_fs.type);
  cv->Put(TableFile::COLUMN_STATUS, file_in_fs.status);
  cv->Put(TableFile::COLUMN_MTIME, file_in_fs.mtime);
  cv->Put(TableFile::COLUMN_LENGTH, file_in_fs.length);
  // copy sha1
  cv->Put(TableFile::COLUMN_SHA1, sha1.c_str(), true);
  cv->Put(TableFile::COLUMN_UNIX_ATTR, file_in_fs.unix_attr);
  cv->Put(TableFile::COLUMN_WIN_ATTR, file_in_fs.win_attr);
  cv->Put(TableFile::COLUMN_ANDROID_ATTR, file_in_fs.android_attr);
  if (type_ == TYPE_BACKUP_DST) {
    cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, 0);
    cv->Put(TableFile::COLUMN_REMOTE_VCLOCK, NULL, 0);
  } else {
    cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, file_in_fs.local_vclock);
  }
  cv->Put(TableFile::COLUMN_PATH, file_in_fs.path(), true);
  cv->Put(TableFile::COLUMN_ALIAS, file_in_fs.alias, true);
  cv->Put(TableFile::COLUMN_MODIFIER, Config::device_name(), true);
  int64_t time_stamp_history = OsTimeInS() - 1;
  cv->Put(TableFile::COLUMN_TIME_STAMP, time_stamp_history);    

  pfile_in_fs->time_stamp = time_stamp_history;
  AddPendingInsertOperation(unique_ptr<FileStat>(pfile_in_fs.release()));

  ApplyBatch();
  
  return ZISYNC_SUCCESS;
}

err_t ReadFsTask::AddRemoveFileOp(unique_ptr<FileStat> pfile_in_db) {//hand over
  FileStat &file_in_db = *pfile_in_db;
  if (file_in_db.status == TableFile::STATUS_REMOVE) {
    return ZISYNC_ERROR_GENERAL;
  }
  ZSLOG_INFO("Remove File(%s)", file_in_db.path());
  has_change = true;
  ContentOperation *cp = op_list.NewUpdate(
      uri_, "%s = %" PRId32 " AND %s = %" PRId64, 
      TableFile::COLUMN_ID, file_in_db.id,
      TableFile::COLUMN_USN, file_in_db.usn);
  cp->SetCondition(this, false);
  ContentValues *cv = cp->GetContentValues();
  cv->Put(TableFile::COLUMN_STATUS, TableFile::STATUS_REMOVE);
  if (type_ == TYPE_RDONLY || type_ == TYPE_BACKUP_DST) {
    cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, 0);
    cv->Put(TableFile::COLUMN_REMOTE_VCLOCK, NULL, 0);
  } else {
    cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, file_in_db.local_vclock + 1);
  }
  cv->Put(TableFile::COLUMN_MODIFIER, Config::device_name(), true);
  cv->Put(TableFile::COLUMN_TIME_STAMP, OsTimeInS() - 1);  
  pfile_in_db->time_stamp = OsTimeInS()-1;
  AddPendingRemoveOperation(unique_ptr<FileStat>(pfile_in_db.release()));

  ApplyBatch();
  
  return ZISYNC_SUCCESS;
}

bool ReadFsTask::Evaluate(ContentOperation *cp) {
  ContentValues *cv = cp->GetContentValues();
  cv->Put(TableFile::COLUMN_USN, GetUsn());
  return true;
}

int32_t ReadFsTask::GetType(const Tree &tree) {
  if (tree.type() == TableTree::BACKUP_DST) {
    return ReadFsTask::TYPE_BACKUP_DST;
  } else {
    int32_t sync_perm = Sync::GetSyncPermByIdWhereStatusNormal(
        tree.sync_id());
    if (sync_perm == -1) {
      ZSLOG_ERROR("Noent Sync(%d)", tree.sync_id());
      return -1;
    }
    if (sync_perm == SYNC_PERM_RDONLY) {
      return  ReadFsTask::TYPE_RDONLY;
    }
  }
  return TYPE_RDWR;
}

void ReadFsTask::AddUpdateOperation(unique_ptr<FileStat> pfile_in_fs){
  const FileStat &file_in_fs = *pfile_in_fs;
  string path = tree_root_ + file_in_fs.path();
  bool isdir = pfile_in_fs->type == zs::OS_FILE_TYPE_DIR ? true : false;
  GetHistoryManager()->AppendHistory(Config::device_name(),
                                           tree_id_, backup_type(), file_in_fs.time_stamp,
                                           path, FILE_OPERATION_ERROR_NONE,
                                           FILE_OPERATION_CODE_MODIFY, NULL, isdir);
}

void ReadFsTask::AddPendingInsertOperation(unique_ptr<FileStat> file_ins) {
  FileStat *ptr = file_ins.release();
  if (events_add_.find(ptr->sha1) == events_add_.end()) {
    events_add_[ptr->sha1] = vector<unique_ptr<FileStat> >();
  }
  events_add_[ptr->sha1].emplace_back(ptr);
}

void ReadFsTask::AddPendingRemoveOperation(unique_ptr<FileStat> file_rm) {
  events_rm_.push_back(std::move(file_rm));
}

void ReadFsTask::ResolvePendingOperations() {

  std::sort(events_rm_.begin(), events_rm_.end(), 
            [] (const unique_ptr<FileStat> &sync1, 
                const unique_ptr<FileStat> &sync2) {
            return sync2->time_stamp > 
            sync1->time_stamp;
            });
  for(map<string, vector<unique_ptr<FileStat> > >::iterator it = events_add_.begin();
          it != events_add_.end(); ++it) {
      std::sort(it->second.begin(), it->second.end(),
              [](const unique_ptr<FileStat> &sync1,
                  const unique_ptr<FileStat> &sync2) {
              return sync1->time_stamp < sync2->time_stamp;
              });
  }
  
  string from, to;
  for(vector<unique_ptr<FileStat> >::iterator it = events_rm_.begin();
      it != events_rm_.end(); ++it ) {
    from = tree_root_ + (*it)->path();
    auto find = events_add_.find((*it)->sha1);

    if(find != events_add_.end() && !find->second.empty()) {
      const unique_ptr<FileStat> &item_ins = find->second.back();
      to = tree_root_ + item_ins->path();
      GetHistoryManager()->AppendHistory(Config::device_name(),
                                             tree_id_, backup_type(), item_ins->time_stamp,
                                             from, FILE_OPERATION_ERROR_NONE,
                                             FILE_OPERATION_CODE_RENAME, to.c_str());
      find->second.pop_back();
    }else{
      GetHistoryManager()->AppendHistory(Config::device_name(),
                                             tree_id_, backup_type(), OsTimeInS(),
                                             from, FILE_OPERATION_ERROR_NONE,
                                             FILE_OPERATION_CODE_DELETE, NULL);
    }
  }

  map<string, vector<unique_ptr<FileStat> > >::const_iterator sha1_it = 
      events_add_.begin();
  for (; sha1_it != events_add_.end(); ++sha1_it) {
    for (vector<unique_ptr<FileStat> >::const_iterator it = 
         sha1_it->second.begin(); it != sha1_it->second.end(); ++it) {
      from = tree_root_ + (*it)->path();  
      bool isdir = (*it)->type == zs::OS_FILE_TYPE_DIR ? true : false;
      GetHistoryManager()->AppendHistory(Config::device_name(),
                                             tree_id_, backup_type(), (*it)->time_stamp,
                                             from, FILE_OPERATION_ERROR_NONE,
                                             FILE_OPERATION_CODE_ADD, NULL, isdir);
    }
  }
  events_add_.clear();
}

}  // namespace zs
