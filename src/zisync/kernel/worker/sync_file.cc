// Copyright 2014, zisync.com

#include <errno.h>
#include <cassert>
#include <memory>

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/worker/sync_file.h"
#include "zisync/kernel/database/icontent.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/utils/usn.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/vector_clock.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/utils/tree.h"
#include "zisync/kernel/utils/device.h"
#include "zisync/kernel/utils/sync.h"
#include "zisync/kernel/utils/rename.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/history/history_manager.h"

namespace zs {

using std::string;
using std::unique_ptr;

static inline void SetCvByFileStatWithoutVclock(
    ContentValues *cv, const FileStat &file_stat) {
  cv->Put(TableFile::COLUMN_TYPE, file_stat.type);
  cv->Put(TableFile::COLUMN_STATUS, file_stat.status);
  cv->Put(TableFile::COLUMN_MTIME, file_stat.mtime);
  cv->Put(TableFile::COLUMN_LENGTH, file_stat.length);
  // copy sha1
  cv->Put(TableFile::COLUMN_SHA1, file_stat.sha1.c_str(), true);
  cv->Put(TableFile::COLUMN_UNIX_ATTR, file_stat.unix_attr);
  cv->Put(TableFile::COLUMN_WIN_ATTR, file_stat.win_attr);
  cv->Put(TableFile::COLUMN_ANDROID_ATTR, file_stat.android_attr);
  cv->Put(TableFile::COLUMN_MODIFIER, file_stat.modifier.c_str(), true);
  cv->Put(TableFile::COLUMN_TIME_STAMP, file_stat.time_stamp);
}

static inline void SetCvByMergeVclock(
    ContentValues *cv, const FileStat &local_file_stat, 
    const FileStat &remote_file_stat) {
  VectorClock merge_vclock(remote_file_stat.vclock);
  merge_vclock.Merge(local_file_stat.vclock);
  cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, merge_vclock.at(0));
  if (merge_vclock.length() > 1) {
    cv->Put(TableFile::COLUMN_REMOTE_VCLOCK, 
            merge_vclock.remote_vclock(), 
            merge_vclock.remote_vclock_size(), true);
  }
}
/*  set all except path */
static inline void SetCvByFileStat(
    ContentValues *cv, const FileStat &file_stat) {
  SetCvByFileStatWithoutVclock(cv, file_stat);
  cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, file_stat.vclock.at(0));
  if (file_stat.vclock.length() > 1) {
    cv->Put(TableFile::COLUMN_REMOTE_VCLOCK, 
            file_stat.vclock.remote_vclock(), 
            file_stat.vclock.remote_vclock_size(), true);
  }
}

static inline void SetCvByRemoteFileStatMergeVclock(
    ContentValues *cv, const FileStat &local_file_stat, 
    const FileStat &remote_file_stat) {
  cv->Put(TableFile::COLUMN_TYPE, remote_file_stat.type);
  cv->Put(TableFile::COLUMN_STATUS, remote_file_stat.status);
  cv->Put(TableFile::COLUMN_MTIME, remote_file_stat.mtime);
  cv->Put(TableFile::COLUMN_LENGTH, remote_file_stat.length);
  // copy sha1
  cv->Put(TableFile::COLUMN_SHA1, remote_file_stat.sha1.c_str(), true);
  cv->Put(TableFile::COLUMN_UNIX_ATTR, remote_file_stat.unix_attr);
  cv->Put(TableFile::COLUMN_WIN_ATTR, remote_file_stat.win_attr);
  cv->Put(TableFile::COLUMN_ANDROID_ATTR, remote_file_stat.android_attr);
  cv->Put(TableFile::COLUMN_MODIFIER, remote_file_stat.modifier.c_str(), true);
  cv->Put(TableFile::COLUMN_TIME_STAMP, remote_file_stat.time_stamp);
  SetCvByMergeVclock(cv, local_file_stat, remote_file_stat);
}

static inline void SetCvMergeFileStat(
    ContentValues *cv, const FileStat &local_file_stat, 
    const FileStat &remote_file_stat) {
  assert(local_file_stat.type == remote_file_stat.type);
  assert(local_file_stat.status == remote_file_stat.status);
  cv->Put(TableFile::COLUMN_MTIME, 
          local_file_stat.mtime > remote_file_stat.mtime ?
          local_file_stat.mtime : remote_file_stat.mtime );
  assert(local_file_stat.length == remote_file_stat.length);
  // copy sha1
  assert(local_file_stat.sha1 == remote_file_stat.sha1);
  cv->Put(TableFile::COLUMN_UNIX_ATTR, 
          local_file_stat.unix_attr | remote_file_stat.unix_attr);
  cv->Put(TableFile::COLUMN_WIN_ATTR, 
          local_file_stat.win_attr | remote_file_stat.win_attr);
  cv->Put(TableFile::COLUMN_ANDROID_ATTR, 
          local_file_stat.android_attr | remote_file_stat.android_attr);
  cv->Put(TableFile::COLUMN_MODIFIER, remote_file_stat.modifier.c_str(), true);
  cv->Put(TableFile::COLUMN_TIME_STAMP, remote_file_stat.time_stamp);
  SetCvByMergeVclock(cv, local_file_stat, remote_file_stat);
}

static inline bool RenameTempFile(
    const string &tmp_file_path, const string &file_path, 
    const FileStat &remote_file_stat) {
  assert(tmp_file_path.length() != 0);

  if (OsChmod(tmp_file_path.c_str(), remote_file_stat.platform_attr) != 0) {
    ZSLOG_ERROR("Chmod(%s) fail : %s", tmp_file_path.c_str(), 
                OsGetLastErr());
    return false;
  }
  assert(OsFileExists(tmp_file_path.c_str()));
  if (OsSetMtime(tmp_file_path.c_str(), remote_file_stat.mtime) != 0) {
    ZSLOG_ERROR("Set (%s) Mtime fail : %s", tmp_file_path.c_str(),
                OsGetLastErr());
    return false;
  }
  if (OsRename(tmp_file_path.c_str(), file_path)!= 0) {
    ZSLOG_ERROR("Rename(%s) to (%s) fail : %s", tmp_file_path.c_str(),
                file_path.c_str(), OsGetLastErr());
    return false;
  }
  return true;
}

static inline bool CreateRemoteDirectory(
    const string dir_path) {
  //string dir_path = local_tree_root + remote_file_stat.path();
  if (OsCreateDirectory(dir_path, false) != 0) {
    ZSLOG_ERROR("Create Dir(%s) fail : %s", dir_path.c_str(),
                OsGetLastErr());
    return false;
  }
  return true;
}

static int PathAppendAdapter(std::string *dest
    , const string &tail) {
  const char *tail_begin = tail.c_str();
  while (*tail_begin == '/') tail_begin++;
  return OsPathAppend(dest, tail_begin);
}

/*  fs and db consistent */
bool SyncFile::IsFileStatConsistent(
    const string &path, const FileStat *local_file_stat) {
  OsFileStat os_stat;
  int ret = OsStat(path, local_file_stat == NULL ? string() : 
                   local_file_stat->alias, &os_stat);
  if (ret != 0 && ret != ENOENT) {
    ZSLOG_ERROR("OsFileStat(%s) fail : %s", path.c_str(), 
                OsGetLastErr());
    return false;
  }
  if (local_file_stat == NULL || 
      local_file_stat->status == TableFile::STATUS_REMOVE) {
    if (ret != ENOENT) {
      ZSLOG_INFO("File(%s) should be noent but not",
                 path.c_str());
      return false;
    } else {
      return true;
    }
  } else {
    if (ret == ENOENT) {
      ZSLOG_INFO("File(%s) should not be noent but is",
                 path.c_str());
      return false;
    }
    if (os_stat.type != local_file_stat->type) {
      ZSLOG_INFO("File(%s)'s type should be (%d) but is (%d)",
                 path.c_str(), local_file_stat->type, os_stat.type);
      return false;
    }
    if (os_stat.type == zs::OS_FILE_TYPE_REG) {
      if (HasMtimeChanged(os_stat.mtime, local_file_stat->mtime)) {
        ZSLOG_INFO("File(%s)'s mtime should be (%" PRId64
                   ") but is (%" PRId64 ")",
                   path.c_str(), local_file_stat->mtime, os_stat.mtime);
        return false;
      }
      if (HasAttrChanged(os_stat.attr, local_file_stat->platform_attr)) {
        ZSLOG_INFO("File(%s)'s type should be (%x) but is (%x)",
                   path.c_str(), local_file_stat->platform_attr, os_stat.attr);
        return false;
      }
    }
  }

  return true;
}

static inline bool DeleteLocalFile(
    const string &file_path, const FileStat &local_file_stat) {
  if (OsDeleteFile(file_path, false) != 0) {
    ZSLOG_ERROR("Delete file(%s) fail : %s", file_path.c_str(),
                OsGetLastErr());
    return false;
  }
  return true;
}

static inline bool DeleteLocalDirectory(
    const string &dir_path, const FileStat &local_file_stat) {
  if (OsDeleteDirectory(dir_path) != 0) {
    ZSLOG_ERROR("Delete Dir(%s) fail : %s", dir_path.c_str(),
                OsGetLastErr());
    return false;
  }
  return true;
}


static inline bool SetRemoteMeta(
    const string &path, const FileStat &remote_file_stat) {
  if (OsSetMtime(path, remote_file_stat.mtime) != 0) {
    ZSLOG_ERROR("Set (%s) Mtime fail : %s", path.c_str(),
                OsGetLastErr());
    return false;
  }
  if (OsChmod(path, remote_file_stat.platform_attr) != 0) {
    ZSLOG_ERROR("Chmod(%s) fail : %s", path.c_str(),  
                OsGetLastErr());
    return false;
  }
  return true;
}

bool SyncFile::Evaluate(ContentOperation *cp) {
  ContentValues *cv = cp->GetContentValues();
  cv->Put(TableFile::COLUMN_USN, GetUsn());
  return true; 
}

class SyncFileInsert : public SyncFile {
 public:
  SyncFileInsert() {}
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);

 private:
  SyncFileInsert(SyncFileInsert&);
  void operator=(SyncFileInsert&);
};


void SyncFileInsert::Handle(
    OperationList *op_list, const char *tmp_root /* = NULL */) {
  assert(local_file_stat_ == NULL);
  assert(remote_file_stat_ != NULL);

  ContentOperation *cp = op_list->NewInsert(*uri_, AOC_IGNORE);
  cp->SetCondition(this, false);
  ContentValues *cv = cp->GetContentValues();
  SetCvByFileStat(cv, *remote_file_stat_);
  cv->Put(TableFile::COLUMN_PATH, remote_file_stat_->path(), true);

  if (tmp_root != NULL) {
    tmp_file_path_.assign(tmp_root);
    tmp_file_path_.append(remote_file_stat_->path());
  }
}

class SyncFileFnInsertData : public SyncFileInsert {
 public: 
  SyncFileFnInsertData() {}
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
  virtual bool Evaluate(ContentOperation *cp);
};

void SyncFileFnInsertData::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileInsert::Handle(op_list, tmp_root);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
            remote_file_stat_->modifier, -1, remote_backup_type(),
            remote_file_stat_->time_stamp, local_full_path,
            FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_ADD,
            NULL);
}

bool SyncFileFnInsertData::Evaluate(ContentOperation *cp) {
  string file_path = local_tree_root_ + remote_file_stat_->path();
  if (!RenameTempFile(tmp_file_path_, file_path, *remote_file_stat_)) {
    ZSLOG_ERROR("RenameTempFile(%s) to (%s) fail.", tmp_file_path_.c_str(),
                file_path.c_str());
    return false;
  }
  return SyncFileInsert::Evaluate(cp);
};

class SyncFileFnInsertMeta : public SyncFileInsert {
 public: 
  SyncFileFnInsertMeta() {}
  virtual bool Evaluate(ContentOperation *cp);
};

bool SyncFileFnInsertMeta::Evaluate(ContentOperation *cp) {
  string file_path = local_tree_root_ + remote_file_stat_->path();
  bool success = SetRemoteMeta(file_path, *remote_file_stat_);
  if (!success) {
    ZSLOG_ERROR("SetRemoteMeta(%s) fail.", file_path.c_str());
    return false;
  }
  return SyncFileInsert::Evaluate(cp);
}

class SyncFileFrInsert : public SyncFileInsert {
 public:
  SyncFileFrInsert() {}
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
 private:
  SyncFileFrInsert(SyncFileFrInsert&);
  void operator=(SyncFileFrInsert&);
};

void SyncFileFrInsert::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileInsert::Handle(op_list, tmp_root);
    if (local_file_stat_) {

      if(forbid_history_from_super_)return;  
      std::string local_full_path = local_tree_root_; 
      PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
      GetHistoryManager()->AppendHistory(
          remote_file_stat_->modifier, -1, remote_backup_type(),
          remote_file_stat_->time_stamp, local_full_path,
          FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_DELETE,
          NULL);
    }

}

class SyncFileDnInsert : public SyncFileInsert {
  public:
    SyncFileDnInsert() {}  
    virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
    virtual bool Evaluate(ContentOperation *cp);
  private:
    SyncFileDnInsert(SyncFileDnInsert&);
    void operator=(SyncFileDnInsert&);
};

void SyncFileDnInsert::Handle (OperationList *op_list,
    const char *tmp_root) {
  SyncFileInsert::Handle(op_list, tmp_root);
  if(forbid_history_from_super_)return;  
  std::string local_full_path = local_tree_root_; 
  PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
  GetHistoryManager()->AppendHistory(
      remote_file_stat_->modifier, -1, remote_backup_type(),
      remote_file_stat_->time_stamp, local_full_path,
      FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_MKDIR,
      NULL);
}

bool SyncFileDnInsert::Evaluate(ContentOperation *cp) {
  string dir_path = local_tree_root_ + remote_file_stat_->path();
  if (!CreateRemoteDirectory(dir_path)) {
    ZSLOG_ERROR("CreateRemoteDirectory(%s) fail.", dir_path.c_str());
    return false;
  } 
  return SyncFileInsert::Evaluate(cp);
}

/* ok */
class SyncFileDrInsert : public SyncFileInsert {//todo:?
  public:
    SyncFileDrInsert() {} 
    virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
  private:
    SyncFileDrInsert(SyncFileDrInsert&);
    void operator=(SyncFileDrInsert&);
};

void SyncFileDrInsert::Handle (OperationList *op_list,
    const char *tmp_root) {
  SyncFileInsert::Handle(op_list, tmp_root);
  if (local_file_stat_) {
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
        remote_file_stat_->modifier, -1, remote_backup_type(),
        remote_file_stat_->time_stamp, local_full_path,
        FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_DELETE,
        NULL);
  }

}

void SyncFileUpdate::Handle(
    OperationList *op_list, const char *tmp_root /* = NULL */) {
  assert(local_file_stat_ != NULL);
  assert(remote_file_stat_ != NULL);

  ContentOperation *cp = op_list->NewUpdate(
      *uri_, "%s = %" PRId32 " AND %s = %" PRId64,
      TableFile::COLUMN_ID, local_file_stat_->id, 
      TableFile::COLUMN_USN, local_file_stat_->usn);
  SetCvByRemoteFileStatMergeVclock(cp->GetContentValues(), 
      *local_file_stat_, *remote_file_stat_);
  cp->SetCondition(this, false);

  if (tmp_root != NULL) {
    tmp_file_path_.assign(tmp_root);
    tmp_file_path_.append(remote_file_stat_->path());
  }
}

/*  only merge the vclcok*/
class SyncFileUpdateVclock : public SyncFile {
  public:
    SyncFileUpdateVclock() {}
    virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);//todo:only vclock?

  private:
    SyncFileUpdateVclock(SyncFileUpdateVclock&);
    void operator=(SyncFileUpdateVclock&);
};

void SyncFileUpdateVclock::Handle(
    OperationList *op_list, const char *tmp_root /* = NULL */) {
  assert(local_file_stat_ != NULL);
  assert(remote_file_stat_ != NULL);

  ContentOperation *cp = op_list->NewUpdate(
      *uri_, "%s = %" PRId32 " AND %s = %" PRId64,
      TableFile::COLUMN_ID, local_file_stat_->id, 
      TableFile::COLUMN_USN, local_file_stat_->usn);
  cp->SetCondition(this, false);
  ContentValues *cv = cp->GetContentValues();
  SetCvByMergeVclock(cv, *local_file_stat_, *remote_file_stat_);
}

/* ok */
class SyncFileFrFrUpdate : public SyncFileUpdate {
  public:
    SyncFileFrFrUpdate() {} 
    virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
  private:
    SyncFileFrFrUpdate(SyncFileFrFrUpdate&);
    void operator=(SyncFileFrFrUpdate&);
};

void SyncFileFrFrUpdate::Handle (OperationList *op_list,
    const char *tmp_root) {
  SyncFileUpdate::Handle(op_list, tmp_root);
  if (local_file_stat_) {
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
        remote_file_stat_->modifier, -1, remote_backup_type(),
        remote_file_stat_->time_stamp, local_full_path,
        FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_DELETE,
        NULL);
  }

}

/* ok */
class SyncFileFrFnUpdateData : public SyncFileUpdate {
  public:
    SyncFileFrFnUpdateData() {} 
    virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
    virtual bool Evaluate(ContentOperation *cp);
  private:
    SyncFileFrFnUpdateData(SyncFileFrFnUpdateData&);
    void operator=(SyncFileFrFnUpdateData&);
};

void SyncFileFrFnUpdateData::Handle (OperationList *op_list,
    const char *tmp_root) {
  SyncFileUpdate::Handle(op_list, tmp_root);
  if(forbid_history_from_super_)return;  
  std::string local_full_path = local_tree_root_; 
  PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
  GetHistoryManager()->AppendHistory(
      remote_file_stat_->modifier, -1, remote_backup_type(),
      remote_file_stat_->time_stamp, local_full_path,
      FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_ADD,
      NULL);
}

bool SyncFileFrFnUpdateData::Evaluate(ContentOperation *cp) {
  string file_path = local_tree_root_ + local_file_stat_->path();
  if (!RenameTempFile(tmp_file_path_, file_path, *remote_file_stat_)) {
    ZSLOG_ERROR("RenameTempFile(%s) to (%s) fail.", tmp_file_path_.c_str(),
        file_path.c_str());
    return false;
  }
  return SyncFileUpdate::Evaluate(cp);
}

/* ok */
class SyncFileFrFnUpdateMeta : public SyncFileUpdate {
  public:
    SyncFileFrFnUpdateMeta() {} 
    virtual bool Evaluate(ContentOperation *cp);
  private:
    SyncFileFrFnUpdateMeta(SyncFileFrFnUpdateMeta&);
    void operator=(SyncFileFrFnUpdateMeta&);
};

bool SyncFileFrFnUpdateMeta::Evaluate(ContentOperation *cp) {
  string file_path = local_tree_root_ + local_file_stat_->path();
  bool success = SetRemoteMeta(file_path, *remote_file_stat_);
  if (!success) {
    ZSLOG_ERROR("SetRemoteMeta(%s) fail.", file_path.c_str());
    return false;
  }
  return SyncFileUpdate::Evaluate(cp);
}

/* ok */
class SyncFileFrDrUpdate : public SyncFileUpdate {
  public:
    SyncFileFrDrUpdate() {}
    void Handle(OperationList *op_list,
        const char *tmp_root = NULL);
  private:
    SyncFileFrDrUpdate(SyncFileFrDrUpdate&);
    void operator=(SyncFileFrDrUpdate&);
};

void SyncFileFrDrUpdate::Handle(OperationList *op_list,
    const char *tmp_root) {
  SyncFileUpdate::Handle(op_list, tmp_root);
  if(forbid_history_from_super_)return;  
  std::string local_full_path = local_tree_root_; 
  PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
  GetHistoryManager()->AppendHistory(
      remote_file_stat_->modifier, -1, remote_backup_type(),
      remote_file_stat_->time_stamp, local_full_path,
      FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_DELETE,
      NULL);
}

/* ok */
class SyncFileFrDnUpdate : public SyncFileUpdate {
  public:
    SyncFileFrDnUpdate() {} 
    virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
    virtual bool Evaluate(ContentOperation *cp);
  private:
    SyncFileFrDnUpdate(SyncFileFrDnUpdate&);
    void operator=(SyncFileFrDnUpdate&);
};

void SyncFileFrDnUpdate::Handle (OperationList *op_list,//todo:what
    const char *tmp_root) {
  SyncFileUpdate::Handle(op_list, tmp_root);
  if(forbid_history_from_super_)return;  
  std::string local_full_path = local_tree_root_; 
  PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
  GetHistoryManager()->AppendHistory(
      remote_file_stat_->modifier, -1, remote_backup_type(),
      remote_file_stat_->time_stamp, local_full_path,
      FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_MKDIR,
      NULL);
}

bool SyncFileFrDnUpdate::Evaluate(ContentOperation *cp) {
  string dir_path = local_tree_root_ + remote_file_stat_->path();
  bool success = CreateRemoteDirectory(dir_path);
  if (!success) {
    ZSLOG_ERROR("CreateRemoteDirectory(%s) fail.", dir_path.c_str());
    return false;
  }
  return SyncFileUpdate::Evaluate(cp);
}

/* ok */
class SyncFileFnFrUpdate : public SyncFileUpdate {
  public:
    SyncFileFnFrUpdate() {} 
    virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
    virtual bool Evaluate(ContentOperation *cp);
  private:
    SyncFileFnFrUpdate(SyncFileFnFrUpdate&);
    void operator=(SyncFileFnFrUpdate&);
};

bool SyncFileFnFrUpdate::Evaluate(ContentOperation *cp) {//add handle
  string file_path = local_tree_root_ + local_file_stat_->path();
  if (!DeleteLocalFile(file_path, *local_file_stat_)) {
    ZSLOG_ERROR("DeleteLocalFile(%s) fail.", local_file_stat_->path());
    return false;
  }
  return SyncFileUpdate::Evaluate(cp);
}

void SyncFileFnFrUpdate::Handle (OperationList *op_list,
    const char *tmp_root) {
  SyncFileUpdate::Handle(op_list, tmp_root);
  assert(remote_file_stat_);
  if(forbid_history_from_super_)return;  
  std::string local_full_path = local_tree_root_; 
  PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
  GetHistoryManager()->AppendHistory(
      remote_file_stat_->modifier, -1, remote_backup_type(),
      remote_file_stat_->time_stamp, local_full_path,
      FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_DELETE,
      NULL);
}

/* ok */
class SyncFileFnFnUpdateMeta : public SyncFileFrFnUpdateMeta {
  public:
    SyncFileFnFnUpdateMeta() {} 
  private:
    SyncFileFnFnUpdateMeta(SyncFileFnFnUpdateMeta&);
    void operator=(SyncFileFnFnUpdateMeta&);
};

/* ok */
class SyncFileFnFnUpdateData : public SyncFileUpdate {
  public:
    SyncFileFnFnUpdateData() {} 
    virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
    virtual bool Evaluate(ContentOperation *cp);
  private:
    SyncFileFnFnUpdateData(SyncFileFnFnUpdateData&);
    void operator=(SyncFileFnFnUpdateData&);
};

void SyncFileFnFnUpdateData::Handle (OperationList *op_list,
    const char *tmp_root) {
  SyncFileUpdate::Handle(op_list, tmp_root);
  assert(remote_file_stat_);
  if(forbid_history_from_super_)return;  
  std::string local_full_path = local_tree_root_; 
  PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
  GetHistoryManager()->AppendHistory(
      remote_file_stat_->modifier, -1, remote_backup_type(),
      remote_file_stat_->time_stamp, local_full_path,
      FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_MODIFY,
      NULL);
}

bool SyncFileFnFnUpdateData::Evaluate(ContentOperation *cp) {
  string file_path = local_tree_root_ + local_file_stat_->path();
  if (!DeleteLocalFile(file_path, *local_file_stat_)) {
    ZSLOG_ERROR("DeleteLocalFile(%s) fail.", file_path.c_str());
    return false;
  }
  if (!RenameTempFile(tmp_file_path_, file_path, *remote_file_stat_)) {
    ZSLOG_ERROR("RenameTempFile(%s) to (%s) fail.", tmp_file_path_.c_str(),
        file_path.c_str());
    return false;
  }
  return SyncFileUpdate::Evaluate(cp);
}

/* ok */
class SyncFileFnDrUpdate : public SyncFileFnFrUpdate {
  public:
    SyncFileFnDrUpdate() {} 
    virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
  private:
    SyncFileFnDrUpdate(SyncFileFnDrUpdate&);
    void operator=(SyncFileFnDrUpdate&);
};

void SyncFileFnDrUpdate::Handle (OperationList *op_list,
    const char *tmp_root) {
  SyncFileFnFrUpdate::Handle(op_list, tmp_root);
  assert(remote_file_stat_);
  if(forbid_history_from_super_)return;  
  std::string local_full_path = local_tree_root_; 
  PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
  GetHistoryManager()->AppendHistory(
      remote_file_stat_->modifier, -1, remote_backup_type(),
      remote_file_stat_->time_stamp, local_full_path,
      FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_DELETE,
            NULL);
}

/* ok */
class SyncFileFnDnUpdate : public SyncFileUpdate {
 public:
  SyncFileFnDnUpdate() {}//todo: when would this happen
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
  virtual bool Evaluate(ContentOperation *cp);
 private:
  SyncFileFnDnUpdate(SyncFileFnDnUpdate&);
  void operator=(SyncFileFnDnUpdate&);
};

void SyncFileFnDnUpdate::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileUpdate::Handle(op_list, tmp_root);
    assert(remote_file_stat_);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
            remote_file_stat_->modifier, -1, remote_backup_type(),
            remote_file_stat_->time_stamp, local_full_path,
            FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_MKDIR,
            NULL);
}

bool SyncFileFnDnUpdate::Evaluate(ContentOperation *cp) {
  string path = local_tree_root_ + remote_file_stat_->path();
  if (!DeleteLocalFile(path, *local_file_stat_)) {
    ZSLOG_ERROR("DeleteLocalFile(%s) fail.", path.c_str());
    return false;
  }
  if (!CreateRemoteDirectory(path)) {
    ZSLOG_ERROR("CreateRemoteDirectory(%s) fail.", path.c_str());
    return false;
  }
  return SyncFileUpdate::Evaluate(cp);
}

/* ok */
class SyncFileDrFrUpdate : public SyncFileUpdate {
 public:
  SyncFileDrFrUpdate() {} 
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
 private:
  SyncFileDrFrUpdate(SyncFileDrFrUpdate&);
  void operator=(SyncFileDrFrUpdate&);
};

void SyncFileDrFrUpdate::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileUpdate::Handle(op_list, tmp_root);
    assert(remote_file_stat_);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
            remote_file_stat_->modifier, -1, remote_backup_type(),
            remote_file_stat_->time_stamp, local_full_path,
            FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_DELETE,
            NULL);
}

/* ok */
class SyncFileDrFnUpdateData : public SyncFileFrFnUpdateData {
 public:
  SyncFileDrFnUpdateData() {}
 private:
  SyncFileDrFnUpdateData(SyncFileDrFnUpdateData&);
  void operator=(SyncFileDrFnUpdateData&);
};

/* ok */
class SyncFileDrFnUpdateMeta : public SyncFileFrFnUpdateMeta {
 public:
  SyncFileDrFnUpdateMeta() {}
 private:
  SyncFileDrFnUpdateMeta(SyncFileDrFnUpdateMeta&);
  void operator=(SyncFileDrFnUpdateMeta&);
};

/* ok */
class SyncFileDrDrUpdate : public SyncFileUpdate {
 public:
  SyncFileDrDrUpdate() {} 
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
 private:
  SyncFileDrDrUpdate(SyncFileDrDrUpdate&);
  void operator=(SyncFileDrDrUpdate&);
};

void SyncFileDrDrUpdate::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileUpdate::Handle(op_list, tmp_root);
    assert(remote_file_stat_);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
            remote_file_stat_->modifier, -1, remote_backup_type(),
            remote_file_stat_->time_stamp, local_full_path,
            FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_DELETE,
            NULL);
}

/* ok */
class SyncFileDrDnUpdate : public SyncFileFrDnUpdate {
 public:
  SyncFileDrDnUpdate() {}
 private:
  SyncFileDrDnUpdate(SyncFileDrDnUpdate&);
  void operator=(SyncFileDrDnUpdate&);
};

/* ok */
class SyncFileDnFrUpdate : public SyncFileUpdate {
 public:
  SyncFileDnFrUpdate() {} 
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
  virtual bool Evaluate(ContentOperation *cp);
 private:
  SyncFileDnFrUpdate(SyncFileDnFrUpdate&);
  void operator=(SyncFileDnFrUpdate&);
};

void SyncFileDnFrUpdate::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileUpdate::Handle(op_list, tmp_root);
    assert(remote_file_stat_);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
            remote_file_stat_->modifier, -1, remote_backup_type(),
            remote_file_stat_->time_stamp, local_full_path,
            FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_DELETE,
            NULL);
}

bool SyncFileDnFrUpdate::Evaluate(ContentOperation *cp) {
  string dir_path = local_tree_root_ + local_file_stat_->path();
  if (!DeleteLocalDirectory(dir_path, *local_file_stat_)) {
    ZSLOG_ERROR("DeleteLocalDirectory(%s) fail.", dir_path.c_str());
    return false;
  }
  return SyncFileUpdate::Evaluate(cp);
}

/* ok */
class SyncFileDnFnUpdate : public SyncFileUpdate {
 public:
  SyncFileDnFnUpdate() {} 
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
  virtual bool Evaluate(ContentOperation *cp);
 private:
  SyncFileDnFnUpdate(SyncFileDnFnUpdate&);
  void operator=(SyncFileDnFnUpdate&);
};


void SyncFileDnFnUpdate::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileUpdate::Handle(op_list, tmp_root);
    assert(remote_file_stat_);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
            remote_file_stat_->modifier, -1, remote_backup_type(),
            remote_file_stat_->time_stamp, local_full_path,
            FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_MODIFY,
            NULL);
}

bool SyncFileDnFnUpdate::Evaluate(ContentOperation *cp) {
  string dir_path = local_tree_root_ + local_file_stat_->path();
  if (!DeleteLocalDirectory(dir_path, *local_file_stat_)) {
    ZSLOG_ERROR("DeleteLocalDirectory(%s) fail.", dir_path.c_str());
    return false;
  }
  if (!RenameTempFile(tmp_file_path_, dir_path, *remote_file_stat_)) {
    ZSLOG_ERROR("RenameTempFile(%s) to (%s) fail.", tmp_file_path_.c_str(),
                dir_path.c_str());
    return false;
  }
  return SyncFileUpdate::Evaluate(cp);
}

/* ok */
class SyncFileDnDrUpdate : public SyncFileDnFrUpdate {
 public:
  SyncFileDnDrUpdate() {}
 private:
  SyncFileDnDrUpdate(SyncFileDnDrUpdate&);
  void operator=(SyncFileDnDrUpdate&);
};

/* ok */
class SyncFileDnDnUpdate : public SyncFileUpdate {
 public:
  SyncFileDnDnUpdate() {} 
 private:
  SyncFileDnDnUpdate(SyncFileDnDnUpdate&);
  void operator=(SyncFileDnDnUpdate&);
};

/* ok */
class SyncFileFrFrConflict : public SyncFileUpdateVclock {
 public:
  SyncFileFrFrConflict() {} 
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
 private:
  SyncFileFrFrConflict(SyncFileFrFrConflict&);
  void operator=(SyncFileFrFrConflict&);
};

void SyncFileFrFrConflict::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileUpdateVclock::Handle(op_list, tmp_root);
    assert(remote_file_stat_);
}

/* ok */
class SyncFileFrFnConflict : public SyncFileFrFnUpdateData {
 public:
  SyncFileFrFnConflict() {}
 private:
  SyncFileFrFnConflict(SyncFileFrFnConflict&);
  void operator=(SyncFileFrFnConflict&);

};

/* ok */
class SyncFileFrDrConflict : public SyncFileUpdateVclock {
 public:
  SyncFileFrDrConflict() {} 
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
 private:
  SyncFileFrDrConflict(SyncFileFrDrConflict&);
  void operator=(SyncFileFrDrConflict&);
};

void SyncFileFrDrConflict::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileUpdateVclock::Handle(op_list, tmp_root);
    assert(remote_file_stat_);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
            remote_file_stat_->modifier, -1, remote_backup_type(),
            remote_file_stat_->time_stamp, local_full_path,
            FILE_OPERATION_ERROR_CONFLICT, FILE_OPERATION_CODE_DELETE,
            NULL);
}

/* ok */
class SyncFileFrDnConflict : public SyncFileFrDnUpdate {//todo:not real conflict?
 public:
  SyncFileFrDnConflict() {}
 private:
  SyncFileFrDnConflict(SyncFileFrDnConflict&);
  void operator=(SyncFileFrDnConflict&);

};

/* ok */
class SyncFileFnFrConflict : public SyncFileUpdateVclock {
 public:
  SyncFileFnFrConflict() {} 
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
 private:
  SyncFileFnFrConflict(SyncFileFnFrConflict&);
  void operator=(SyncFileFnFrConflict&);
};

void SyncFileFnFrConflict::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileUpdateVclock::Handle(op_list, tmp_root);
    assert(remote_file_stat_);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
            remote_file_stat_->modifier, -1, remote_backup_type(),
            remote_file_stat_->time_stamp, local_full_path,
            FILE_OPERATION_ERROR_CONFLICT, FILE_OPERATION_CODE_DELETE,
            NULL);
}

/* ok */
class SyncFileFnFnConflictMeta : public SyncFile {
 public:
  SyncFileFnFnConflictMeta() {}
  virtual bool Evaluate(ContentOperation *cp);
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);

 private:
  SyncFileFnFnConflictMeta(SyncFileFnFnConflictMeta&);
  void operator=(SyncFileFnFnConflictMeta&);
};

void SyncFileFnFnConflictMeta::Handle(
    OperationList *op_list, const char *tmp_root /* = NULL */) {
  assert(local_file_stat_ != NULL);
  assert(remote_file_stat_ != NULL);

  ContentOperation *cp = op_list->NewUpdate(
      *uri_, "%s = %" PRId32 " AND %s = %" PRId64,
      TableFile::COLUMN_ID, local_file_stat_->id, 
      TableFile::COLUMN_USN, local_file_stat_->usn);
  cp->SetCondition(this, false);
  SetCvMergeFileStat(cp->GetContentValues(), 
                     *local_file_stat_, *remote_file_stat_);
//  if(forbid_history_from_super_)return;  
//  std::string local_full_path = local_tree_root_; 
//  PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
//  GetHistoryManager()->AppendHistory(
//            remote_file_stat_->modifier, -1, remote_backup_type(),
//            remote_file_stat_->time_stamp, local_full_path,
//            FILE_OPERATION_ERROR_CONFLICT, FILE_OPERATION_CODE_ATTRIB,
//            NULL);
}

bool SyncFileFnFnConflictMeta::Evaluate(ContentOperation *cp) {
  string file_path = local_tree_root_ + local_file_stat_->path();
  if (remote_file_stat_->mtime > local_file_stat_->mtime) {
    if (OsSetMtime(file_path, remote_file_stat_->mtime) != 0) {
      ZSLOG_ERROR("SetMtime(%s) fail : %s", file_path.c_str(), 
                  OsGetLastErr());
      return false;
    }
  }
  if (remote_file_stat_->platform_attr != local_file_stat_->platform_attr) {
    if (OsChmod(file_path, remote_file_stat_->platform_attr |
                local_file_stat_->platform_attr) != 0) {
      ZSLOG_ERROR("Chmod(%s) fail : %s", file_path.c_str(), OsGetLastErr());
      return false;
    }
  }

  return SyncFile::Evaluate(cp);
}

/* ok */
class SyncFileFnFnConflictDataMvLocalOnLocal : 
    public SyncFile {
     public:
      SyncFileFnFnConflictDataMvLocalOnLocal(const Device *remote_device):
          remote_device_(remote_device) , is_root_class_(true){}
      virtual bool Evaluate(ContentOperation *cp);
      virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);

     private:
      SyncFileFnFnConflictDataMvLocalOnLocal(SyncFileFnFnConflictDataMvLocalOnLocal&);
      void operator=(SyncFileFnFnConflictDataMvLocalOnLocal&);

      string conflict_file_path_;
      const Device *remote_device_;
     protected:
      bool is_root_class_;
    };

void SyncFileFnFnConflictDataMvLocalOnLocal::Handle(//todo
    OperationList *op_list, const char *tmp_root /* = NULL */) {
  assert(local_file_stat_ != NULL);
  assert(remote_file_stat_ != NULL);

  // string conflict_desc;
  // StringFormat(&conflict_desc, "%s-%s", Config::device_uuid().c_str(), 
  //              remote_device_.uuid().c_str());

  // GenConflictFilePath(
  //     &conflict_file_path_, local_tree_root_, conflict_desc, 
  //     local_file_stat_->path());
  GenConflictFilePath(
      &conflict_file_path_, local_tree_root_, local_file_stat_->path());
  IContentResolver *resolver = zs::GetContentResolver();
  const char *projs[] = {
    TableFile::COLUMN_ID, TableFile::COLUMN_USN, TableFile::COLUMN_LOCAL_VCLOCK, 
  };
  std::unique_ptr<ICursor2> cursor(resolver->Query(
          *uri_, projs, ARRAY_SIZE(projs), 
          "%s = '%s'", TableFile::COLUMN_PATH, 
          GenFixedStringForDatabase(conflict_file_path_).c_str()));
  if (cursor->MoveToNext()) {
    ContentOperation *cp = op_list->NewUpdate(
        *uri_, "%s = %" PRId32 " AND %s = %" PRId64,
        TableFile::COLUMN_ID, cursor->GetInt32(0), 
        TableFile::COLUMN_USN, cursor->GetInt64(1));
    ContentValues *cv = cp->GetContentValues();
    SetCvByFileStatWithoutVclock(cv, *local_file_stat_);
    cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, cursor->GetInt32(2) + 1);

    cp->SetCondition(this, false);
  } else {
    ContentOperation *cp = op_list->NewInsert(*uri_, AOC_IGNORE);
    ContentValues *cv = cp->GetContentValues();
    SetCvByFileStatWithoutVclock(cv, *local_file_stat_);
    cv->Put(TableFile::COLUMN_PATH, conflict_file_path_.c_str(), true);
    cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, 1);
    cp->SetCondition(this, false);
  }
  

}

bool SyncFileFnFnConflictDataMvLocalOnLocal::Evaluate(ContentOperation *cp) {
  assert(conflict_file_path_.length() != 0);
  string file_path = local_tree_root_ + local_file_stat_->path();
  string conflict_file_absolute_path = local_tree_root_ + conflict_file_path_;

  if (OsExists(conflict_file_absolute_path)) {
    ZSLOG_INFO("File(%s) expected to be noent but not.", 
               conflict_file_path_.c_str());
    return false;
  }
  if (OsRename(file_path, conflict_file_absolute_path) != 0) {
    ZSLOG_ERROR("Rename(%s) to (%s) fail : %s", file_path.c_str(),
                conflict_file_absolute_path.c_str(), OsGetLastErr());
    return false;
  }

  return SyncFile::Evaluate(cp);
}

/* ok */
class SyncFileFnFnConflictDataMvLocalOnRemote : public SyncFileUpdate {
 public:
  SyncFileFnFnConflictDataMvLocalOnRemote() {}
  virtual bool Evaluate(ContentOperation *cp);

 private:
  SyncFileFnFnConflictDataMvLocalOnRemote(
      SyncFileFnFnConflictDataMvLocalOnRemote&);
  void operator=(SyncFileFnFnConflictDataMvLocalOnRemote&);
};

bool SyncFileFnFnConflictDataMvLocalOnRemote::Evaluate(ContentOperation *cp) {
  string file_path = local_tree_root_ + local_file_stat_->path();
  if (!RenameTempFile(tmp_file_path_, file_path, *remote_file_stat_)) {
    ZSLOG_ERROR("RenameTempFile(%s) to (%s) fail.", tmp_file_path_.c_str(),
                file_path.c_str());
    return false;
  }
  return SyncFileUpdate::Evaluate(cp);
}

class SyncFileFnFnConflictDataMvLocal : public SyncFile {
 public:
  SyncFileFnFnConflictDataMvLocal(const Device *remote_device):
      local(remote_device) {}
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL) {//todo
    local.Handle(op_list, tmp_root);
    remote.Handle(op_list, tmp_root);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
        Config::device_name(), -1, local_backup_type(),
        local_file_stat_->time_stamp, local_file_stat_->path(),
        FILE_OPERATION_ERROR_CONFLICT, FILE_OPERATION_CODE_MODIFY,
        NULL);
  }

  virtual void set_local_file_stat(FileStat *local_file_stat) {
    remote.set_local_file_stat(local_file_stat);
    local.set_local_file_stat(local_file_stat);
    local_file_stat_ = local_file_stat;
  }
  virtual void set_remote_file_stat(FileStat *remote_file_stat) {
    local.set_remote_file_stat(remote_file_stat);
    remote.set_remote_file_stat(remote_file_stat);
    remote_file_stat_ = remote_file_stat;
  }
  virtual void set_local_tree_root(const std::string &local_tree_root) {
    local.set_local_tree_root(local_tree_root);
    remote.set_local_tree_root(local_tree_root);
  }
  virtual void set_uri(const Uri *uri) {
    local.set_uri(uri);
    remote.set_uri(uri);
  }

 private:
  SyncFileFnFnConflictDataMvLocalOnLocal local;
  SyncFileFnFnConflictDataMvLocalOnRemote remote;
};

/* ok */
class SyncFileFnFnConflictDataMvRemoteOnLocal : public SyncFileUpdateVclock {
 public:
  SyncFileFnFnConflictDataMvRemoteOnLocal() {}

 private:
  SyncFileFnFnConflictDataMvRemoteOnLocal(
      SyncFileFnFnConflictDataMvRemoteOnLocal&);
  void operator=(SyncFileFnFnConflictDataMvRemoteOnLocal&);
};

/* ok */
class SyncFileFnFnConflictDataMvRemoteOnRemote : 
    public SyncFile {
     public:
      SyncFileFnFnConflictDataMvRemoteOnRemote(const Device *remote_device):
          remote_device_(remote_device) {}
      virtual bool Evaluate(ContentOperation *cp);
      virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);

     private:
      SyncFileFnFnConflictDataMvRemoteOnRemote(
          SyncFileFnFnConflictDataMvRemoteOnRemote&);
      void operator=(SyncFileFnFnConflictDataMvRemoteOnRemote&);

      string tmp_file_path_, conflict_file_path_;
      const Device *remote_device_;
    };

void SyncFileFnFnConflictDataMvRemoteOnRemote::Handle(//todo
    OperationList *op_list, const char *tmp_root /* = NULL */) {
  assert(tmp_root != NULL);

  // string conflict_desc;
  // StringFormat(&conflict_desc, "%s-%s", remote_device_.uuid().c_str(),
  //              Config::device_uuid().c_str());
  // GenConflictFilePath(
  //     &conflict_file_path_, local_tree_root_ , conflict_desc, 
  //     local_file_stat_->path());
  GenConflictFilePath(
      &conflict_file_path_, local_tree_root_ , local_file_stat_->path());
  IContentResolver *resolver = zs::GetContentResolver();
  const char *projs[] = {
    TableFile::COLUMN_ID, TableFile::COLUMN_USN, TableFile::COLUMN_LOCAL_VCLOCK, 
  };
  std::unique_ptr<ICursor2> cursor(resolver->Query(
          *uri_, projs, ARRAY_SIZE(projs), 
          "%s = '%s'", TableFile::COLUMN_PATH, 
          GenFixedStringForDatabase(conflict_file_path_).c_str()));
  if (cursor->MoveToNext()) {
    ContentOperation *cp = op_list->NewUpdate(
        *uri_, "%s = %" PRId32 " AND %s = %" PRId64,
        TableFile::COLUMN_ID, cursor->GetInt32(0), 
        TableFile::COLUMN_USN, cursor->GetInt64(1));
    ContentValues *cv = cp->GetContentValues();
    SetCvByFileStatWithoutVclock(cv, *remote_file_stat_);
    cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, cursor->GetInt32(2) + 1);
    cp->SetCondition(this, false);
  } else {
    ContentOperation *cp = op_list->NewInsert(*uri_, AOC_IGNORE);
    ContentValues *cv = cp->GetContentValues();
    SetCvByFileStatWithoutVclock(cv, *remote_file_stat_);
    cv->Put(TableFile::COLUMN_PATH, conflict_file_path_.c_str(), true);
    cv->Put(TableFile::COLUMN_LOCAL_VCLOCK, 1);
    cp->SetCondition(this, false);
  }

  tmp_file_path_.assign(tmp_root);
  tmp_file_path_.append(remote_file_stat_->path());
}

bool SyncFileFnFnConflictDataMvRemoteOnRemote::Evaluate(ContentOperation *cp) {//todo
  string conflict_file_absolute_path = local_tree_root_ + conflict_file_path_;
  if (OsExists(conflict_file_absolute_path)) {
    ZSLOG_INFO("File(%s) expected to be noent but not.", 
               conflict_file_absolute_path.c_str());
    return false;
  }
  if (OsSetMtime(tmp_file_path_.c_str(), remote_file_stat_->mtime) != 0) {
    ZSLOG_ERROR("Set (%s) Mtime fail : %s", remote_file_stat_->path(),
                OsGetLastErr());
    return false;
  }
  if (OsChmod(tmp_file_path_.c_str(), remote_file_stat_->platform_attr) != 0) {
    ZSLOG_ERROR("Chmod(%s) fail : %s", remote_file_stat_->path(), 
                OsGetLastErr());
    return false;
  }
  if (OsRename(tmp_file_path_.c_str(), 
               conflict_file_absolute_path.c_str()) != 0) {
    ZSLOG_ERROR("Rename(%s) to (%s) fail : %s", tmp_file_path_.c_str(),
                conflict_file_absolute_path.c_str(), OsGetLastErr());
    return false;
  }
  return SyncFile::Evaluate(cp);
}

class SyncFileFnFnConflictDataMvRemote : public SyncFile {
 public:
  SyncFileFnFnConflictDataMvRemote(const Device *remote_device):
      remote(remote_device) {}
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL) {
    local.Handle(op_list, tmp_root);
    remote.Handle(op_list, tmp_root);
    //todo
    if(forbid_history_from_super_)return;
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
        remote_file_stat_->modifier, -1, remote_backup_type(),
        remote_file_stat_->time_stamp, local_full_path,
        FILE_OPERATION_ERROR_CONFLICT, FILE_OPERATION_CODE_MODIFY,
        NULL);
  }

  virtual void set_local_file_stat(FileStat *local_file_stat) {
    remote.set_local_file_stat(local_file_stat);
    local.set_local_file_stat(local_file_stat);
    local_file_stat_ = local_file_stat;
  }
  virtual void set_remote_file_stat(FileStat *remote_file_stat) {
    local.set_remote_file_stat(remote_file_stat);
    remote.set_remote_file_stat(remote_file_stat);
    remote_file_stat_ = remote_file_stat;
  }
  virtual void set_local_tree_root(const std::string &local_tree_root) {
    local.set_local_tree_root(local_tree_root);
    remote.set_local_tree_root(local_tree_root);
  }
  virtual void set_uri(const Uri *uri) {
    local.set_uri(uri);
    remote.set_uri(uri);
  }

 private:
  SyncFileFnFnConflictDataMvRemoteOnLocal local;
  SyncFileFnFnConflictDataMvRemoteOnRemote remote;
};


/* ok *///what does this could mean
class SyncFileFnDrConflict : public SyncFileUpdateVclock {
 public:
  SyncFileFnDrConflict() {}
 private:
  SyncFileFnDrConflict(SyncFileFnDrConflict&);
  void operator=(SyncFileFnDrConflict&);
};

/* ok */
class SyncFileFnDnConflictOnLocal : 
    public SyncFileFnFnConflictDataMvLocalOnLocal {
     public:
      SyncFileFnDnConflictOnLocal(const Device *remote_device):
          SyncFileFnFnConflictDataMvLocalOnLocal(remote_device) {
            is_root_class_ = false;
          }
     private:
      SyncFileFnDnConflictOnLocal(SyncFileFnDnConflictOnLocal&);
      void operator=(SyncFileFnDnConflictOnLocal&);
    };


/* ok */
class SyncFileFnDnConflictOnRemote : public SyncFileFrDnUpdate {
 public:
  SyncFileFnDnConflictOnRemote() {}
 private:
  SyncFileFnDnConflictOnRemote(SyncFileFnDnConflictOnLocal&);
  void operator=(SyncFileFnDnConflictOnRemote&);
};

class SyncFileFnDnConflict : public SyncFile {
 public:
  SyncFileFnDnConflict(const Device *device):local(device) {}
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL) {
    local.Handle(op_list, tmp_root);
    remote.Handle(op_list, tmp_root);

    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
      Config::device_name(), -1, local_backup_type(),
      local_file_stat_->time_stamp, local_file_stat_->path(),
      FILE_OPERATION_ERROR_CONFLICT, FILE_OPERATION_CODE_MODIFY,
      NULL);
  }

  virtual void set_local_file_stat(FileStat *local_file_stat) {
    remote.set_local_file_stat(local_file_stat);
    local.set_local_file_stat(local_file_stat);
    local_file_stat_ = local_file_stat;
  }
  virtual void set_remote_file_stat(FileStat *remote_file_stat) {
    local.set_remote_file_stat(remote_file_stat);
    remote.set_remote_file_stat(remote_file_stat);
    remote_file_stat_ = remote_file_stat;
  }
  virtual void set_local_tree_root(const std::string &local_tree_root) {
    local.set_local_tree_root(local_tree_root);
    remote.set_local_tree_root(local_tree_root);
  }
  virtual void set_uri(const Uri *uri) {
    local.set_uri(uri);
    remote.set_uri(uri);
  }

 private:
  SyncFileFnDnConflictOnLocal local;
  SyncFileFnDnConflictOnRemote remote;
};

/* ok */
class SyncFileDrFrConflict : public SyncFileUpdateVclock {
 public:
  SyncFileDrFrConflict() {} 
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
 private:
  SyncFileDrFrConflict(SyncFileDrFrConflict&);
  void operator=(SyncFileDrFrConflict&);
};

void SyncFileDrFrConflict::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileUpdateVclock::Handle(op_list, tmp_root);
    assert(remote_file_stat_);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
            remote_file_stat_->modifier, -1, remote_backup_type(),
            remote_file_stat_->time_stamp, local_full_path,
            FILE_OPERATION_ERROR_CONFLICT, FILE_OPERATION_CODE_DELETE,
            NULL);
}

/* ok */
class SyncFileDrFnConflict : public SyncFileFrFnUpdateData {
 public:
  SyncFileDrFnConflict() {}
 private:
  SyncFileDrFnConflict(SyncFileDrFnConflict&);
  void operator=(SyncFileDrFnConflict&);
};

/* ok */
class SyncFileDrDrConflict : public SyncFileUpdateVclock {
 public:
  SyncFileDrDrConflict() {} 
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
 private:
  SyncFileDrDrConflict(SyncFileDrDrConflict&);
  void operator=(SyncFileDrDrConflict&);
};

void SyncFileDrDrConflict::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileUpdateVclock::Handle(op_list, tmp_root);
    assert(remote_file_stat_);
}

/* ok */
class SyncFileDrDnConflict : public SyncFileFrDnUpdate {//todo
 public:
  SyncFileDrDnConflict() {}
 private:
  SyncFileDrDnConflict(SyncFileDrDnConflict&);
  void operator=(SyncFileDrDnConflict&);
};

/* ok */
class SyncFileDnFrConflict : public SyncFileUpdateVclock {
 public:
  SyncFileDnFrConflict() {} 
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
 private:
  SyncFileDnFrConflict(SyncFileDnFrConflict&);
  void operator=(SyncFileDnFrConflict&);
};

void SyncFileDnFrConflict::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileUpdateVclock::Handle(op_list, tmp_root);
    assert(remote_file_stat_);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
            remote_file_stat_->modifier, -1, remote_backup_type(),
            remote_file_stat_->time_stamp, local_full_path,
            FILE_OPERATION_ERROR_CONFLICT, FILE_OPERATION_CODE_DELETE,
            NULL);
}


/* ok */
class SyncFileDnFnConflictOnLocal : public SyncFileUpdateVclock {
 public:
  SyncFileDnFnConflictOnLocal() {}
 private:
  SyncFileDnFnConflictOnLocal(SyncFileDnFnConflictOnLocal&);
  void operator=(SyncFileDnFnConflictOnLocal&);
};

/*  ok */
class SyncFileDnFnConflictOnRemote : 
    public SyncFileFnFnConflictDataMvRemoteOnRemote {
     public:
      SyncFileDnFnConflictOnRemote(const Device *remote_device):
          SyncFileFnFnConflictDataMvRemoteOnRemote(remote_device) {}
     private:
      SyncFileDnFnConflictOnRemote(SyncFileDnFnConflictOnRemote&);
      void operator=(SyncFileDnFnConflictOnRemote&);
    };

class SyncFileDnFnConflict : public SyncFile {
 public:
  SyncFileDnFnConflict(const Device *remote_device):remote(remote_device) {}
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL) {
    local.Handle(op_list, tmp_root);
    remote.Handle(op_list, tmp_root);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
            remote_file_stat_->modifier, -1, remote_backup_type(),
            remote_file_stat_->time_stamp, local_full_path,
            FILE_OPERATION_ERROR_CONFLICT, FILE_OPERATION_CODE_MODIFY,
            NULL);
  }

  virtual void set_local_file_stat(FileStat *local_file_stat) {
    remote.set_local_file_stat(local_file_stat);
    local.set_local_file_stat(local_file_stat);
    local_file_stat_ = local_file_stat;
  }
  virtual void set_remote_file_stat(FileStat *remote_file_stat) {
    local.set_remote_file_stat(remote_file_stat);
    remote.set_remote_file_stat(remote_file_stat);
    remote_file_stat_ = remote_file_stat;
  }
  virtual void set_local_tree_root(const std::string &local_tree_root) {
    local.set_local_tree_root(local_tree_root);
    remote.set_local_tree_root(local_tree_root);
  }
  virtual void set_uri(const Uri *uri) {
    local.set_uri(uri);
    remote.set_uri(uri);
  }

 private:
  SyncFileDnFnConflictOnLocal local;
  SyncFileDnFnConflictOnRemote remote;
};

/*  ok */
class SyncFileDnDrConflict : public SyncFileUpdateVclock {
 public:
  SyncFileDnDrConflict() {}  
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);
 private:
  SyncFileDnDrConflict(SyncFileDnDrConflict&);
  void operator=(SyncFileDnDrConflict&);
};

void SyncFileDnDrConflict::Handle (OperationList *op_list,
        const char *tmp_root) {
    SyncFileUpdateVclock::Handle(op_list, tmp_root);
    if(forbid_history_from_super_)return;  
    std::string local_full_path = local_tree_root_; 
    PathAppendAdapter(&local_full_path, remote_file_stat_->path()); 
    GetHistoryManager()->AppendHistory(
            remote_file_stat_->modifier, -1, remote_backup_type(),
            remote_file_stat_->time_stamp, local_full_path,
            FILE_OPERATION_ERROR_CONFLICT, FILE_OPERATION_CODE_DELETE,
            NULL);
}

/*  ok */
class SyncFileDnDnConflict : public SyncFileUpdateVclock {
 public:
  SyncFileDnDnConflict() {} 
 private:
  SyncFileDnDnConflict(SyncFileDnDnConflict&);
  void operator=(SyncFileDnDnConflict&);
};

SyncFileFnFrUpdateRenameFrom::SyncFileFnFrUpdateRenameFrom(SyncFile *sync_file) {
  local_file_stat_ = sync_file->mutable_local_file_stat();
  remote_file_stat_ = sync_file->mutable_remote_file_stat();
  local_tree_root_ = sync_file->local_tree_root();
  uri_ = sync_file->uri();
  mask_ = sync_file->mask();
}

bool SyncFileFnFrUpdateRenameFrom::Evaluate(ContentOperation *cp) {
  return SyncFileUpdate::Evaluate(cp);
}

SyncFile* SyncFile::Create(
    const Tree &local_tree, const Tree &remote_tree,
    const Device *remote_device, int sync_file_mask) {
  SyncFile *sync_file;
  switch (sync_file_mask) {
    case SYNC_FILE_FR_INSERT_META         : 
      sync_file = new SyncFileFrInsert(); 
      break;
    case SYNC_FILE_FN_INSERT_DATA         : 
      sync_file = new SyncFileFnInsertData(); 
      break;
    case SYNC_FILE_FN_INSERT_META        : 
      sync_file = new SyncFileFnInsertMeta(); 
      break;
    case SYNC_FILE_DR_INSERT_META        : 
      sync_file = new SyncFileDrInsert(); 
      break;
    case SYNC_FILE_DN_INSERT_META         : 
      sync_file = new SyncFileDnInsert(); 
      break;
    case SYNC_FILE_FR_FR_UPDATE_META      : 
      sync_file = new SyncFileFrFrUpdate(); 
      break;
    case SYNC_FILE_FR_FN_UPDATE_DATA      : 
      sync_file = new SyncFileFrFnUpdateData(); 
      break;
    case SYNC_FILE_FR_FN_UPDATE_META      : 
      sync_file = new SyncFileFrFnUpdateMeta(); //used in Rdonly 
      break;
    case SYNC_FILE_FR_DR_UPDATE_META      : 
      sync_file = new SyncFileFrDrUpdate(); 
      break;
    case SYNC_FILE_FR_DN_UPDATE_META      : 
      sync_file = new SyncFileFrDnUpdate(); 
      break;
    case SYNC_FILE_FN_FR_UPDATE_META      : 
      sync_file = new SyncFileFnFrUpdate(); 
      break;
    case SYNC_FILE_FN_FN_UPDATE_META      :
      sync_file = new SyncFileFnFnUpdateMeta(); 
      break;
    case SYNC_FILE_FN_FN_UPDATE_DATA      :
      sync_file = new SyncFileFnFnUpdateData(); 
      break;
    case SYNC_FILE_FN_DR_UPDATE_META      : 
      sync_file = new SyncFileFnDrUpdate(); 
      break;
    case SYNC_FILE_FN_DN_UPDATE_META      : 
      sync_file = new SyncFileFnDnUpdate(); 
      break;
    case SYNC_FILE_DR_FR_UPDATE_META      : 
      sync_file = new SyncFileDrFrUpdate(); 
      break;
    case SYNC_FILE_DR_FN_UPDATE_DATA      : 
      sync_file = new SyncFileDrFnUpdateData(); 
      break;
    case SYNC_FILE_DR_FN_UPDATE_META      : 
      sync_file = new SyncFileDrFnUpdateMeta(); 
      break;
    case SYNC_FILE_DR_DR_UPDATE_META      : 
      sync_file = new SyncFileDrDrUpdate(); 
      break;
    case SYNC_FILE_DR_DN_UPDATE_META      : 
      sync_file = new SyncFileDrDnUpdate(); 
      break;
    case SYNC_FILE_DN_FR_UPDATE_META      : 
      sync_file = new SyncFileDnFrUpdate(); 
      break;
    case SYNC_FILE_DN_FN_UPDATE_DATA      : 
      sync_file = new SyncFileDnFnUpdate(); 
      break;
    case SYNC_FILE_DN_DR_UPDATE_META      : 
      sync_file = new SyncFileDnDrUpdate(); 
      break;
    case SYNC_FILE_DN_DN_UPDATE_META      : 
      sync_file = new SyncFileDnDnUpdate(); 
      break;
    case SYNC_FILE_FR_FR_CONFLICT_META    :
      sync_file = new SyncFileFrFrConflict(); 
      break;
    case SYNC_FILE_FR_FN_CONFLICT_DATA   :
      sync_file = new SyncFileFrFnConflict(); 
      break;
    case SYNC_FILE_FR_DR_CONFLICT_META   :
      sync_file = new SyncFileFrDrConflict(); 
      break;
    case SYNC_FILE_FR_DN_CONFLICT_META    :
      sync_file = new SyncFileFrDnConflict(); 
      break;
    case SYNC_FILE_FN_FR_CONFLICT_META    :
      sync_file = new SyncFileFnFrConflict(); 
      break;
    case SYNC_FILE_FN_FN_CONFLICT_META    :
      sync_file = new SyncFileFnFnConflictMeta(); 
      break;
    case SYNC_FILE_FN_FN_CONFLICT_DATA    :
      if (local_tree.uuid() < remote_tree.uuid()) {//todo:why
        sync_file = new SyncFileFnFnConflictDataMvLocal(remote_device); 
      } else {
        sync_file = new SyncFileFnFnConflictDataMvRemote(remote_device); 
      }
      break;
    case SYNC_FILE_FN_DR_CONFLICT_META    :
      sync_file = new SyncFileFnDrConflict(); break;
    case SYNC_FILE_FN_DN_CONFLICT_META    :
      sync_file = new SyncFileFnDnConflict(remote_device); break;
    case SYNC_FILE_DR_FR_CONFLICT_META    :
      sync_file = new SyncFileDrFrConflict(); break;
    case SYNC_FILE_DR_FN_CONFLICT_DATA    :
      sync_file = new SyncFileDrFnConflict(); break;
    case SYNC_FILE_DR_DR_CONFLICT_META    :
      sync_file = new SyncFileDrDrConflict(); break;
    case SYNC_FILE_DR_DN_CONFLICT_META    :
      sync_file = new SyncFileDrDnConflict(); break;
    case SYNC_FILE_DN_FR_CONFLICT_META    :
      sync_file = new SyncFileDnFrConflict(); break;
    case SYNC_FILE_DN_FN_CONFLICT_DATA    :
      sync_file = new SyncFileDnFnConflict(remote_device); break;
    case SYNC_FILE_DN_DR_CONFLICT_META   :
      sync_file = new SyncFileDnDrConflict(); break;
    case SYNC_FILE_DN_DN_CONFLICT_META    :
      sync_file = new SyncFileDnDnConflict(); break;
    default:
      ZSLOG_ERROR("Invalid SyncMask(%x)", sync_file_mask);
      assert(false);
  }
  sync_file->set_mask(sync_file_mask);
  sync_file->set_remote_device(remote_tree.device_id());
  sync_file->set_remote_tree_root(remote_tree.root());
  sync_file->set_loca_backup_type(local_tree.type());
  sync_file->set_remote_backup_type(remote_tree.type());

  return sync_file;
}

void SyncFile::SetSyncFileMask(
    const FileStat *local_file_stat, const FileStat *remote_file_stat,
    int *sync_mask) {
  if (remote_file_stat != NULL) {
    if (remote_file_stat->type == OS_FILE_TYPE_REG) {
      MaskSetRemoteReg(sync_mask);
    }else {
      MaskSetRemoteDir(sync_mask);
    }
    if (remote_file_stat->status == TableFile::STATUS_REMOVE) {
      MaskSetRemoteRemove(sync_mask);
    }else {
      MaskSetRemoteNormal(sync_mask);
    }
  }

  if (local_file_stat != NULL) {
    if (local_file_stat->type == OS_FILE_TYPE_REG) {
      MaskSetLocalReg(sync_mask);
    }else {
      MaskSetLocalDir(sync_mask);
    }
    if (local_file_stat->status == TableFile::STATUS_REMOVE) {
      MaskSetLocalRemove(sync_mask);
    }else {
      MaskSetLocalNormal(sync_mask);
    }
  }
}

bool SyncFile::IsBackupSrcRemove(
    const FileStat *local_file_stat, const FileStat *remote_file_stat,
    int sync_mask, int local_tree_backup_type) {
  assert(remote_file_stat != NULL);
  return (local_tree_backup_type == TableTree::BACKUP_DST &&
          MaskIsRemoteRemove(sync_mask)) ||
      (local_tree_backup_type == TableTree::BACKUP_SRC &&
       local_file_stat != NULL && MaskIsLocalRemove(sync_mask));//todo: is this bug
}

bool SyncFile::IsBackupDstInsert(
    const FileStat *local_file_stat, const FileStat *remote_file_stat,
    int sync_mask, int local_tree_backup_type) {
  assert(remote_file_stat != NULL);
  return (local_tree_backup_type == TableTree::BACKUP_SRC &&
          local_file_stat == NULL);
}

void SyncFileRename::Handle(
    OperationList *op_list, const char *tmp_root /* = NULL */) {
  assert(tmp_root == NULL);
  sync_file_from_->forbid_history_from_super_ = true;
  sync_file_from_->Handle(op_list);
  assert(sync_file_from_->local_file_stat() != NULL);
  sync_file_to_->set_tmp_file_path(
      local_tree_root_ + sync_file_from_->local_file_stat()->path()); 
  sync_file_to_->forbid_history_from_super_ = true;
  sync_file_to_->Handle(op_list);
  if(forbid_history_from_super_)return;  

  std::string local_full_path_from = local_tree_root_;
  std::string local_full_path_to = local_tree_root_; 
  PathAppendAdapter(&local_full_path_from, sync_file_from_->remote_file_stat()->path());
  PathAppendAdapter(&local_full_path_to, sync_file_to_->remote_file_stat()->path()); 

  GetHistoryManager()->AppendHistory(
            sync_file_to_->remote_file_stat()->modifier, -1, remote_backup_type(),
            sync_file_to_->remote_file_stat()->time_stamp, local_full_path_from,
            FILE_OPERATION_ERROR_NONE, FILE_OPERATION_CODE_RENAME,
            local_full_path_to.c_str());
}

static inline bool RenameLocalFileToConflict(
    const string &local_tree_root, const string &relative_path) {
  string conflict_file_path;
  string conflict_desc;
  // StringFormat(&conflict_desc, "%s-%s", Config::device_uuid().c_str(), 
  //              remote_device_uuid.c_str());
  GenConflictFilePath(
      &conflict_file_path, local_tree_root, relative_path);
  string conflict_file_absolute_path = 
      local_tree_root + conflict_file_path;
  const string file_path = local_tree_root + relative_path;
  if (OsRename(file_path, conflict_file_absolute_path) != 0) {
    ZSLOG_ERROR("Rename(%s) to (%s) fail : %s", file_path.c_str(),
                conflict_file_absolute_path.c_str(), OsGetLastErr());
    return false;
  }
  return true;
}

void LocalFileConsistentHandler::GenNewSyncFile(
    unique_ptr<SyncFile> *sync_file, int new_sync_mask) {
  SyncFile *new_sync_file = SyncFile::Create(
      local_tree_, remote_tree_, new_sync_mask);
  new_sync_file->set_local_file_stat(
      (*sync_file)->mutable_local_file_stat());
  new_sync_file->set_remote_file_stat(
      (*sync_file)->mutable_remote_file_stat());
  new_sync_file->set_local_tree_root((*sync_file)->local_tree_root());
  new_sync_file->set_uri((*sync_file)->uri());
  sync_file->reset(new_sync_file);
}

bool LocalFileConsistentHandler::LocalRegRemoteNormalReg(
    unique_ptr<SyncFile> *sync_file, const OsFileStat &os_stat) {
  if (!RenameLocalFileToConflict(
          local_tree_.root(), (*sync_file)->remote_file_stat()->path())) {
    ZSLOG_ERROR("RenameLocalFileToConflict fail");
    return false;
  }
  int new_sync_mask = (*sync_file)->mask();
  if (!(*sync_file)->MaskIsInsert()) {
    SyncFile::MaskSetLocalReg(&new_sync_mask);
    SyncFile::MaskSetLocalRemove(&new_sync_mask);
  }
  SyncFile::MaskSetData(&new_sync_mask);
  GenNewSyncFile(sync_file, new_sync_mask);
  UpdateLocalFileStatBySyncMask(
      (*sync_file)->mutable_local_file_stat(), new_sync_mask);
  return true;
}

// bool LocalFileConsistentHandler::LocalRegRemoteNormalRegSha1Same(
//     const OsFileStat &os_stat) {
//   int new_sync_mask = (*sync_file_)->mask();
//   SyncFile::MaskSetLocalReg(&new_sync_mask);
//   SyncFile::MaskSetLocalNormal(&new_sync_mask);
//   SyncFile::MaskSetMeta(&new_sync_mask);
//   GenNewSyncFile(new_sync_mask);
//   UpdateLocalFileStat(os_stat);
//   return true;
// }

bool LocalFileConsistentHandler::LocalRegRemoteNormalDir(
    unique_ptr<SyncFile> *sync_file, const OsFileStat &os_stat) {
  if (!RenameLocalFileToConflict(
          local_tree_.root(), (*sync_file)->remote_file_stat()->path())) {
    ZSLOG_ERROR("RenameLocalFileToConflict fail");
    return false;
  }
  int new_sync_mask = (*sync_file)->mask();
  if (!(*sync_file)->MaskIsInsert()) {
    SyncFile::MaskSetLocalReg(&new_sync_mask);
    SyncFile::MaskSetLocalRemove(&new_sync_mask);
  }
  SyncFile::MaskSetMeta(&new_sync_mask);
  GenNewSyncFile(sync_file, new_sync_mask);
  UpdateLocalFileStatBySyncMask(
      (*sync_file)->mutable_local_file_stat(), new_sync_mask);
  return true;
}
  
bool LocalFileConsistentHandler::LocalDirRemoteNormalReg(
    std::unique_ptr<SyncFile> *sync_file, const OsFileStat &os_stat) {
  int new_sync_mask = (*sync_file)->mask();
  if (!(*sync_file)->MaskIsInsert()) {
    SyncFile::MaskSetLocalRemove(&new_sync_mask);
  }
  SyncFile::MaskSetMeta(&new_sync_mask);
  GenNewSyncFile(sync_file, new_sync_mask);
  return true;
}


bool LocalFileConsistentHandler::LocalDirRemoteNormalDir(
    unique_ptr<SyncFile> *sync_file, const OsFileStat &os_stat) {
  int new_sync_mask = (*sync_file)->mask();
  if (!(*sync_file)->MaskIsInsert()) {
    SyncFile::MaskSetLocalDir(&new_sync_mask);
    SyncFile::MaskSetLocalNormal(&new_sync_mask);
  }
  SyncFile::MaskSetMeta(&new_sync_mask);
  GenNewSyncFile(sync_file, new_sync_mask);
  return true;
}

bool LocalFileConsistentHandler::RemoteRemove(
    unique_ptr<SyncFile> *sync_file, const OsFileStat &os_stat) {
  int new_sync_mask = (*sync_file)->mask();
  if (!(*sync_file)->MaskIsInsert()) {
    SyncFile::MaskSetLocalRemove(&new_sync_mask);
  }
  SyncFile::MaskSetMeta(&new_sync_mask);
  GenNewSyncFile(sync_file, new_sync_mask);
  return true;
}

void LocalFileConsistentHandler::UpdateLocalFileStat(
    FileStat *local_file_stat, const OsFileStat &file_stat) {
  assert(local_file_stat != NULL);
  local_file_stat->status = TableFile::STATUS_NORMAL;
  local_file_stat->type = file_stat.type;
  local_file_stat->mtime = file_stat.mtime;
  local_file_stat->platform_attr = file_stat.attr;
}

void LocalFileConsistentHandler::UpdateLocalFileStatBySyncMask(
    FileStat *local_file_stat, int sync_mask) {
  if (local_file_stat == NULL) {
    return;
  }
  local_file_stat->status = SyncFile::MaskIsLocalNormal(sync_mask) ?
      TableFile::STATUS_NORMAL : TableFile::STATUS_REMOVE;
  local_file_stat->type = SyncFile::MaskIsLocalReg(sync_mask) ? 
      OS_FILE_TYPE_REG: OS_FILE_TYPE_DIR;
}

bool LocalFileConsistentHandler::Handle(unique_ptr<SyncFile> *sync_file) {
  assert(*sync_file);
  assert((*sync_file)->remote_file_stat() != NULL);
  
  string local_file_path_ = local_tree_.root() +
      (*sync_file)->remote_file_stat()->path();
  if (SyncFile::IsFileStatConsistent(
          local_file_path_, (*sync_file)->local_file_stat())) {
    return true;
  } else if (sync_.perm() != SYNC_PERM_RDONLY) {
    return false;
  }

  // not consistent and is SYNC_PERM_RDONLY
  OsFileStat os_stat;
  string alias = (*sync_file)->local_file_stat() == NULL ? 
      string() : (*sync_file)->local_file_stat()->alias;
  int ret = OsStat(local_file_path_, alias, &os_stat);
  if (ret != 0 ) {
    ZSLOG_ERROR("OsFileStat(%s) fail : %s", local_file_path_.c_str(), 
                OsGetLastErr());
    return false;
  }

  if ((*sync_file)->local_file_stat() != NULL &&
      (*sync_file)->MaskIsLocalNormal() && 
      (*sync_file)->MaskIsLocalReg() && os_stat.type == OS_FILE_TYPE_REG) {
    const FileStat *file_in_db = (*sync_file)->local_file_stat();
    string local_file_sha1;
    if ((os_stat.mtime != file_in_db->mtime) ||
        (os_stat.length != file_in_db->length)) {
      err_t zisync_ret = FileSha1(
          local_file_path_.c_str(), alias, &local_file_sha1);
      if (zisync_ret != ZISYNC_SUCCESS) {
        ZSLOG_ERROR("FileSha1(%s) fail : %s", local_file_path_.c_str(), 
                    zisync_strerror(zisync_ret));
        return false;
      }
      if ((*sync_file)->local_file_stat()->sha1 == local_file_sha1) {
        ZSLOG_INFO("Meta change but data not change");
        UpdateLocalFileStat(
            (*sync_file)->mutable_local_file_stat(), os_stat);
        return true;
      }
    } else {
      ZSLOG_INFO("Meta change but data not change");
      UpdateLocalFileStat(
          (*sync_file)->mutable_local_file_stat(), os_stat);
      return true;
    }
  }

  if ((*sync_file)->MaskIsRemoteNormal()) {
    if ((*sync_file)->MaskIsRemoteReg()) {
      if (os_stat.type == OS_FILE_TYPE_REG) {
        // local is file
        // if (!GetLocalFileSha1()) {
        //   ZSLOG_ERROR("GetLocalFileSha1 fail");
        //   return false;
        // }
        // if (local_file_sha1_ != (*sync_file)->remote_file_stat()->sha1) {
        //   return LocalRegRemoteNormalRegSha1Diff(os_stat);
        // } else {
        //   // the same sha1, set sync file a meta
        //   return LocalRegRemoteNormalRegSha1Same(os_stat);
        // }
        return LocalRegRemoteNormalReg(sync_file, os_stat);
      } else {
        return LocalDirRemoteNormalReg(sync_file, os_stat);
      }
    } else { // remote_dir
      if (os_stat.type == OS_FILE_TYPE_REG) { // local file
        return LocalRegRemoteNormalDir(sync_file, os_stat);
      } else {  // local dir
        return LocalDirRemoteNormalDir(sync_file, os_stat);
      }
    }
  } else {
    // remote is remove, remain the local file
    return RemoteRemove(sync_file, os_stat);
  }
}

bool LocalFileConsistentHandler::HandleRenameFrom(
    unique_ptr<SyncFile> *sync_file_from) {
  string local_file_from_path = local_tree_.root() +
      (*sync_file_from)->remote_file_stat()->path();
  
  OsFileStat os_stat;
  string alias = 
      (*sync_file_from)->local_file_stat() == NULL ? 
      string() : (*sync_file_from)->local_file_stat()->alias;
  int ret = OsStat(local_file_from_path, alias, &os_stat);
  if (ret != 0 ) {
    ZSLOG_ERROR("OsFileStat(%s) fail : %s", local_file_from_path.c_str(), 
                OsGetLastErr());
    sync_file_from->reset(NULL);
    return false;
  }

  assert((*sync_file_from)->local_file_stat() != NULL);
  if (os_stat.type == OS_FILE_TYPE_REG) {
    const FileStat *file_in_db = (*sync_file_from)->local_file_stat();
    if ((os_stat.mtime != file_in_db->mtime) ||
        (os_stat.length != file_in_db->length)) {
      string local_file_sha1;
      err_t zisync_ret = FileSha1(
          local_file_from_path.c_str(), alias, &local_file_sha1);
      if (zisync_ret != ZISYNC_SUCCESS) {
        ZSLOG_ERROR("FileSha1(%s) fail : %s", local_file_from_path.c_str(), 
                    zisync_strerror(zisync_ret));
        sync_file_from->reset(NULL);
        return false;
      }
      if ((*sync_file_from)->local_file_stat()->sha1 == local_file_sha1) {
        ZSLOG_INFO("Meta change but data not change");
        UpdateLocalFileStat(
            (*sync_file_from)->mutable_local_file_stat(), os_stat);
        return true;
      }
    } else {
      ZSLOG_INFO("Meta change but data not change");
      UpdateLocalFileStat(
          (*sync_file_from)->mutable_local_file_stat(), os_stat);
      return true;
    }
  }
  RemoteRemove(sync_file_from, os_stat);
  return false;
}

bool LocalFileConsistentHandler::Handle(unique_ptr<SyncFileRename> *sync_file) {
  assert(*sync_file);
  
  SyncFile *sync_file_from = (*sync_file)->sync_file_from_.get();

  string local_file_from_path = local_tree_.root() +
      (*sync_file)->sync_file_from_->remote_file_stat()->path();
  string local_file_to_path = local_tree_.root() +
      (*sync_file)->sync_file_to_->remote_file_stat()->path();
  bool is_from_consistent = SyncFile::IsFileStatConsistent(
          local_file_from_path, 
          (*sync_file)->sync_file_from_->local_file_stat());
  bool is_to_consistent = SyncFile::IsFileStatConsistent(
          local_file_to_path, 
          (*sync_file)->sync_file_to_->local_file_stat());
  if (sync_.perm() != SYNC_PERM_RDONLY) {
    bool is_to_consistent = SyncFile::IsFileStatConsistent(
        local_file_to_path, 
        (*sync_file)->sync_file_to_->local_file_stat());
    return is_from_consistent && is_to_consistent;
  } else {
    assert(sync_file_from->local_file_stat() != NULL);
    assert(sync_file_from->MaskIsLocalNormal());
    assert(sync_file_from->MaskIsLocalReg());
    assert(sync_file_from->MaskIsRemoteReg());
    assert(sync_file_from->MaskIsRemoteRemove());

    // handle from
    if (!is_from_consistent && 
        !HandleRenameFrom(&(*sync_file)->sync_file_from_)) {
      ZSLOG_INFO("RenameFrom is not consistent");
      is_from_consistent = false;
    } else {
      is_from_consistent = true;
    }

    if (is_from_consistent) {
      assert(IsRenameFrom((*sync_file)->sync_file_from_.get()));
    }

    // handle to
    if (!is_to_consistent && 
        !HandleRenameTo(&(*sync_file)->sync_file_to_)) {
      ZSLOG_INFO("RenameTo is not consistent");
      is_to_consistent = false;
      if (is_from_consistent) {
        GenNewSyncFile(
            &(*sync_file)->sync_file_from_, SYNC_FILE_FN_FR_UPDATE_META);
      }
    } else {
      is_to_consistent = true;
      assert(IsRenameTo((*sync_file)->sync_file_to_.get()));
    }
    return is_from_consistent && is_to_consistent;
  }
}

bool LocalFileConsistentHandler::HandleRenameTo(
    unique_ptr<SyncFile> *sync_file_to) {
  assert((*sync_file_to)->MaskIsRemoteNormal());
  assert((*sync_file_to)->MaskIsRemoteReg());
  string local_file_to_path = local_tree_.root() +
      (*sync_file_to)->remote_file_stat()->path();
  
  OsFileStat os_stat;
  int ret = OsStat(
      local_file_to_path, (*sync_file_to)->local_file_stat() == NULL ? 
      string() : (*sync_file_to)->local_file_stat()->alias, &os_stat);
  if (ret != 0 ) {
    ZSLOG_ERROR("OsFileStat(%s) fail : %s", local_file_to_path.c_str(), 
                OsGetLastErr());
    sync_file_to->reset(NULL);
    return false;
  }

  if (os_stat.type == OS_FILE_TYPE_REG) {
    return LocalRegRemoteNormalReg(sync_file_to, os_stat);
  } else {
    LocalDirRemoteNormalReg(sync_file_to, os_stat);
    return false;
  }
}

}  // namespace zs
