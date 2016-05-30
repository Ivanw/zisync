// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_WORKER_SYNC_FILE_H_
#define ZISYNC_KERNEL_WORKER_SYNC_FILE_H_

#include <memory>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/utils/vector_clock.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/database/icontent.h"

namespace zs {

class FileStat;
class OperationList;
class Uri;
class Tree;
class Sync;
class Device;
class OsFileStat;

const int SYNC_FILE_FR_INSERT_META      = 0x00;
const int SYNC_FILE_FN_INSERT_DATA      = 0x41;
const int SYNC_FILE_FN_INSERT_META      = 0x01;
const int SYNC_FILE_DR_INSERT_META      = 0x02;
const int SYNC_FILE_DN_INSERT_META      = 0x03;
const int SYNC_FILE_FR_FR_UPDATE_META   = 0x10;
const int SYNC_FILE_FR_FN_UPDATE_DATA   = 0x51;
const int SYNC_FILE_FR_FN_UPDATE_META   = 0x11;
const int SYNC_FILE_FR_DR_UPDATE_META   = 0x12;
const int SYNC_FILE_FR_DN_UPDATE_META   = 0x13;
const int SYNC_FILE_FN_FR_UPDATE_META   = 0x14;
const int SYNC_FILE_FN_FN_UPDATE_META   = 0x15;
const int SYNC_FILE_FN_FN_UPDATE_DATA   = 0x55;
const int SYNC_FILE_FN_DR_UPDATE_META   = 0x16;
const int SYNC_FILE_FN_DN_UPDATE_META   = 0x17;
const int SYNC_FILE_DR_FR_UPDATE_META   = 0x18;
const int SYNC_FILE_DR_FN_UPDATE_DATA   = 0x59;
const int SYNC_FILE_DR_FN_UPDATE_META   = 0x19;
const int SYNC_FILE_DR_DR_UPDATE_META   = 0x1a;
const int SYNC_FILE_DR_DN_UPDATE_META   = 0x1b;
const int SYNC_FILE_DN_FR_UPDATE_META   = 0x1c;
const int SYNC_FILE_DN_FN_UPDATE_DATA   = 0x5d;
const int SYNC_FILE_DN_DR_UPDATE_META   = 0x1e;
const int SYNC_FILE_DN_DN_UPDATE_META   = 0x1f;
const int SYNC_FILE_FR_FR_CONFLICT_META = 0x20;
const int SYNC_FILE_FR_FN_CONFLICT_DATA = 0x61;
const int SYNC_FILE_FR_DR_CONFLICT_META = 0x22;
const int SYNC_FILE_FR_DN_CONFLICT_META = 0x23;
const int SYNC_FILE_FN_FR_CONFLICT_META = 0x24;
const int SYNC_FILE_FN_FN_CONFLICT_META = 0x25;
const int SYNC_FILE_FN_FN_CONFLICT_DATA = 0x65;
const int SYNC_FILE_FN_DR_CONFLICT_META = 0x26;
const int SYNC_FILE_FN_DN_CONFLICT_META = 0x27;
const int SYNC_FILE_DR_FR_CONFLICT_META = 0x28;
const int SYNC_FILE_DR_FN_CONFLICT_DATA = 0x69;
const int SYNC_FILE_DR_DR_CONFLICT_META = 0x2a;
const int SYNC_FILE_DR_DN_CONFLICT_META = 0x2b;
const int SYNC_FILE_DN_FR_CONFLICT_META = 0x2c;
const int SYNC_FILE_DN_FN_CONFLICT_DATA = 0x6d;
const int SYNC_FILE_DN_DR_CONFLICT_META = 0x2e;
const int SYNC_FILE_DN_DN_CONFLICT_META = 0x2f;
const int SYNC_FILE_RENAME_META         = 0x30;

const int SYNC_FILE_REMOTE_NORMAL =  0x1;
const int SYNC_FILE_REMOTE_REMOVE = ~0x1;
const int SYNC_FILE_REMOTE_DIR    =  0x2;
const int SYNC_FILE_REMOTE_REG    = ~0x2;
const int SYNC_FILE_LOCAL_NORMAL  =  0x4;
const int SYNC_FILE_LOCAL_REMOVE  = ~0x4;
const int SYNC_FILE_LOCAL_DIR     =  0x8;
const int SYNC_FILE_LOCAL_REG     = ~0x8;
const int SYNC_FILE_DATA          =  0x40;
const int SYNC_FILE_META          = ~0x40;

inline const char* str_sync_mask(int sync_mask) {
  switch (sync_mask) {
    case SYNC_FILE_FR_INSERT_META      :
      return "SYNC_FILE_FR_INSERT_META";
    case SYNC_FILE_FN_INSERT_DATA      :
      return "SYNC_FILE_FN_INSERT_DATA";
    case SYNC_FILE_FN_INSERT_META      :
      return "SYNC_FILE_FN_INSERT_META";
    case SYNC_FILE_DR_INSERT_META      :
      return "SYNC_FILE_DR_INSERT_META";
    case SYNC_FILE_DN_INSERT_META      :
      return "SYNC_FILE_DN_INSERT_META";
    case SYNC_FILE_FR_FR_UPDATE_META   :
      return "SYNC_FILE_FR_FR_UPDATE_META";
    case SYNC_FILE_FR_FN_UPDATE_DATA   :
      return "SYNC_FILE_FR_FN_UPDATE_DATA";
    case SYNC_FILE_FR_FN_UPDATE_META   :
      return "SYNC_FILE_FR_FN_UPDATE_META";
    case SYNC_FILE_FR_DR_UPDATE_META   :
      return "SYNC_FILE_FR_DR_UPDATE_META";
    case SYNC_FILE_FR_DN_UPDATE_META   :
      return "SYNC_FILE_FR_DN_UPDATE_META";
    case SYNC_FILE_FN_FR_UPDATE_META   :
      return "SYNC_FILE_FN_FR_UPDATE_META";
    case SYNC_FILE_FN_FN_UPDATE_META   :
      return "SYNC_FILE_FN_FN_UPDATE_META";
    case SYNC_FILE_FN_FN_UPDATE_DATA   :
      return "SYNC_FILE_FN_FN_UPDATE_DATA";
    case SYNC_FILE_FN_DR_UPDATE_META   :
      return "SYNC_FILE_FN_DR_UPDATE_META";
    case SYNC_FILE_FN_DN_UPDATE_META   :
      return "SYNC_FILE_FN_DN_UPDATE_META";
    case SYNC_FILE_DR_FR_UPDATE_META   :
      return "SYNC_FILE_DR_FR_UPDATE_META";
    case SYNC_FILE_DR_FN_UPDATE_DATA   :
      return "SYNC_FILE_DR_FN_UPDATE_DATA";
    case SYNC_FILE_DR_FN_UPDATE_META   :
      return "SYNC_FILE_DR_FN_UPDATE_META";
    case SYNC_FILE_DR_DR_UPDATE_META   :
      return "SYNC_FILE_DR_DR_UPDATE_META";
    case SYNC_FILE_DR_DN_UPDATE_META   :
      return "SYNC_FILE_DR_DN_UPDATE_META";
    case SYNC_FILE_DN_FR_UPDATE_META   :
      return "SYNC_FILE_DN_FR_UPDATE_META";
    case SYNC_FILE_DN_FN_UPDATE_DATA   :
      return "SYNC_FILE_DN_FN_UPDATE_DATA";
    case SYNC_FILE_DN_DR_UPDATE_META   :
      return "SYNC_FILE_DN_DR_UPDATE_META";
    case SYNC_FILE_DN_DN_UPDATE_META   :
      return "SYNC_FILE_DN_DN_UPDATE_META";
    case SYNC_FILE_FR_FR_CONFLICT_META :
      return "SYNC_FILE_FR_FR_CONFLICT_META";
    case SYNC_FILE_FR_FN_CONFLICT_DATA :
      return "SYNC_FILE_FR_FN_CONFLICT_DATA";
    case SYNC_FILE_FR_DR_CONFLICT_META :
      return "SYNC_FILE_FR_DR_CONFLICT_META";
    case SYNC_FILE_FR_DN_CONFLICT_META :
      return "SYNC_FILE_FR_DN_CONFLICT_META";
    case SYNC_FILE_FN_FR_CONFLICT_META :
      return "SYNC_FILE_FN_FR_CONFLICT_META";
    case SYNC_FILE_FN_FN_CONFLICT_META :
      return "SYNC_FILE_FN_FN_CONFLICT_META";
    case SYNC_FILE_FN_FN_CONFLICT_DATA :
      return "SYNC_FILE_FN_FN_CONFLICT_DATA";
    case SYNC_FILE_FN_DR_CONFLICT_META :
      return "SYNC_FILE_FN_DR_CONFLICT_META";
    case SYNC_FILE_FN_DN_CONFLICT_META :
      return "SYNC_FILE_FN_DN_CONFLICT_META";
    case SYNC_FILE_DR_FR_CONFLICT_META :
      return "SYNC_FILE_DR_FR_CONFLICT_META";
    case SYNC_FILE_DR_FN_CONFLICT_DATA :
      return "SYNC_FILE_DR_FN_CONFLICT_DATA";
    case SYNC_FILE_DR_DR_CONFLICT_META :
      return "SYNC_FILE_DR_DR_CONFLICT_META";
    case SYNC_FILE_DR_DN_CONFLICT_META :
      return "SYNC_FILE_DR_DN_CONFLICT_META";
    case SYNC_FILE_DN_FR_CONFLICT_META :
      return "SYNC_FILE_DN_FR_CONFLICT_META";
    case SYNC_FILE_DN_FN_CONFLICT_DATA :
      return "SYNC_FILE_DN_FN_CONFLICT_DATA";
    case SYNC_FILE_DN_DR_CONFLICT_META :
      return "SYNC_FILE_DN_DR_CONFLICT_META";
    case SYNC_FILE_DN_DN_CONFLICT_META :
      return "SYNC_FILE_DN_DN_CONFLICT_META";
    default:
      ZSLOG_ERROR("Invalid mask : %x", sync_mask);
      assert(false);
      return NULL;
  }
}

class SyncFile : public OperationCondition {
 public:
  /*  local_file_stat and remote_file_stat does not copy, just copy the pointer
   *  address */
  SyncFile():forbid_history_from_super_(false),
    local_file_stat_(NULL), remote_file_stat_(NULL), uri_(NULL){}
  /*  TODO define as virtual not impleted and not used ? */
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL) = 0;
  static SyncFile* Create(
      const Tree &local_tree, const Tree &remote_tree, 
      const Device &remote_device, int sync_file_mask) {
    return Create(local_tree, remote_tree, &remote_device, sync_file_mask);
  }
  static SyncFile* Create(
      const Tree &local_tree, const Tree &remote_tree, 
      int sync_file_mask) {
    return Create(local_tree, remote_tree, NULL, sync_file_mask);
  }
  virtual void set_local_file_stat(FileStat *local_file_stat) {
    local_file_stat_ = local_file_stat;
  }
  virtual void set_remote_file_stat(FileStat *remote_file_stat) {
    remote_file_stat_ = remote_file_stat;
  }
  virtual void set_local_tree_root(const std::string &local_tree_root) {
    local_tree_root_.assign(local_tree_root);
  }
  virtual void set_remote_tree_root(const std::string &remote_tree_root) {
      remote_tree_root_.assign(remote_tree_root);
  }
  virtual void set_remote_device(int32_t device_id) {
    remote_device_ = device_id;
  }
  virtual void set_uri(const Uri *uri) {
    uri_ = uri;
  }
  virtual void set_loca_backup_type(int32_t type) {local_backup_type_ = type;}
  int32_t local_backup_type() {return local_backup_type_;}
  virtual void set_remote_backup_type(int32_t type) { remote_backup_type_ = type; }
  int32_t remote_backup_type() {return remote_backup_type_;}

  void set_mask(int mask) { mask_ = mask; }
  const FileStat* local_file_stat() const { return local_file_stat_; }
  const FileStat* remote_file_stat() const { return remote_file_stat_; }
  int mask() const { return mask_; }
  const std::string& local_tree_root() const { return local_tree_root_; }
  const Uri* uri() const { return uri_; }
  FileStat* mutable_local_file_stat() { return local_file_stat_; }
  FileStat* mutable_remote_file_stat() { return remote_file_stat_; }

  static bool MaskIsRemoteReg(int sync_mask) {
    return ~(sync_mask | SYNC_FILE_REMOTE_REG) ? true : false;
  }
  static bool MaskIsLocalReg(int sync_mask) {
    return ~(sync_mask | SYNC_FILE_LOCAL_REG) ? true : false;
  }
  static bool MaskIsLocalNormal(int sync_mask) {
    return sync_mask & SYNC_FILE_LOCAL_NORMAL ? true : false;
  }
  static bool MaskIsLocalRemove(int sync_mask) {
    return ~(sync_mask | SYNC_FILE_LOCAL_REMOVE) ? true : false;
  }
  static bool MaskIsRemoteNormal(int sync_mask) {
    return sync_mask & SYNC_FILE_REMOTE_NORMAL ? true : false;
  }
  static bool MaskIsRemoteRemove(int sync_mask) {
    return ~(sync_mask | SYNC_FILE_REMOTE_REMOVE) ? true : false;
  }
  static bool MaskIsData(int sync_mask) {
    return sync_mask & SYNC_FILE_DATA ? true : false;
  }
  static bool MaskIsMeta(int sync_mask) {
    return ~(sync_mask | SYNC_FILE_META) ? true : false;
  }
  static bool MaskIsInsert(int sync_mask) {//no mask ?
    return (sync_mask >> 4) == 0x0;
  }
  static bool MaskIsUpdate(int sync_mask) {
    return (sync_mask >> 4) == 0x1;
  }
  static bool MaskIsConflict(int sync_mask) {
    return (sync_mask >> 4) == 0x2;
  }
  static void MaskSetLocalNormal(int *sync_mask) {
    // ZSLOG_INFO("set local normal");
    *sync_mask |= SYNC_FILE_LOCAL_NORMAL;
  }
  static void MaskSetLocalRemove(int *sync_mask) {
    // ZSLOG_INFO("set local remove");
    *sync_mask &= SYNC_FILE_LOCAL_REMOVE;
  }
  static void MaskSetLocalDir(int *sync_mask) {
    // ZSLOG_INFO("set local dir");
    *sync_mask |= SYNC_FILE_LOCAL_DIR;
  }
  static void MaskSetLocalReg(int *sync_mask) {
    // ZSLOG_INFO("set local reg");
    *sync_mask &= SYNC_FILE_LOCAL_REG;
  }
  static void MaskSetRemoteNormal(int *sync_mask) {
    // ZSLOG_INFO("set remote normal");
    *sync_mask |= SYNC_FILE_REMOTE_NORMAL;
  }
  static void MaskSetRemoteRemove(int *sync_mask) {
    // ZSLOG_INFO("set remote remove");
    *sync_mask &= SYNC_FILE_REMOTE_REMOVE;
  }
  static void MaskSetRemoteDir(int *sync_mask) {
    // ZSLOG_INFO("set remote dir");
    *sync_mask |= SYNC_FILE_REMOTE_DIR;
  }
  static void MaskSetRemoteReg(int *sync_mask) {
    // ZSLOG_INFO("set remote reg");
    *sync_mask &= SYNC_FILE_REMOTE_REG;
  }
  static void MaskSetData(int *sync_mask) {
    // ZSLOG_INFO("set data");
    *sync_mask |= SYNC_FILE_DATA;
  }
  static void MaskSetMeta(int *sync_mask) {
    // ZSLOG_INFO("set meta");
    *sync_mask &= SYNC_FILE_META;
  }
  static void MaskSetInsert(int *sync_mask) {
    // ZSLOG_INFO("set insert");
    *sync_mask &= ~(0x30);
  }
  static void MaskSetUpdate(int *sync_mask) {
    // ZSLOG_INFO("set update");
    *sync_mask = (((*sync_mask) | (1 << 4)) & (~(1 << 5)));
  }
  static void MaskSetConflict(int *sync_mask) {
    // ZSLOG_INFO("set conflict");
    *sync_mask = (((*sync_mask) & (~(1 << 4))) | (1 << 5));
  }
  
  bool MaskIsRemoteReg() { return MaskIsRemoteReg(mask_); }
  bool MaskIsLocalReg() { return MaskIsLocalReg(mask_); }
  bool MaskIsLocalNormal() { return MaskIsLocalNormal(mask_); }
  bool MaskIsLocalRemove() { return MaskIsLocalRemove(mask_); }
  bool MaskIsRemoteNormal() { return MaskIsRemoteNormal(mask_); }
  bool MaskIsRemoteRemove() { return MaskIsRemoteRemove(mask_); }
  bool MaskIsData() { return MaskIsData(mask_); }
  bool MaskIsMeta() { return MaskIsMeta(mask_); }
  bool MaskIsInsert() { return MaskIsInsert(mask_); }
  bool MaskIsUpdate() { return MaskIsUpdate(mask_); }
  bool MaskIsConflict() { return MaskIsConflict(mask_); }
  void MaskSetLocalNormal() { MaskSetLocalNormal(&mask_); }
  void MaskSetLocalRemove() { MaskSetLocalRemove(&mask_); }
  void MaskSetLocalDir() { MaskSetLocalDir(&mask_); }
  void MaskSetLocalReg() { MaskSetLocalReg(&mask_); }
  void MaskSetRemoteNormal() { MaskSetRemoteNormal(&mask_); }
  void MaskSetRemoteRemove() { MaskSetRemoteRemove(&mask_); }
  void MaskSetRemoteDir() { MaskSetRemoteDir(&mask_); }
  void MaskSetRemoteReg() { MaskSetRemoteReg(&mask_); }
  void MaskSetData() { MaskSetData(&mask_); }
  void MaskSetMeta() { MaskSetMeta(&mask_); }
  void MaskSetInsert() { MaskSetInsert(&mask_); }
  void MaskSetUpdate() { MaskSetUpdate(&mask_); }
  void MaskSetConflict() { MaskSetConflict(&mask_); }

  static bool IsBackupNotSync(
      const FileStat *local_file_stat, const FileStat *remote_file_stat,
      int sync_mask, int local_tree_back_type) {
    return IsBackupSrcRemove(
        local_file_stat, remote_file_stat, sync_mask, local_tree_back_type) ||
        IsBackupDstInsert(
            local_file_stat, remote_file_stat, sync_mask, local_tree_back_type);
  }
  static bool IsBackupSrcRemove(
      const FileStat *local_file_stat, const FileStat *remote_file_stat,
      int sync_mask, int local_tree_backup_type);
  
  static bool IsBackupDstInsert(
      const FileStat *local_file_stat, const FileStat *remote_file_stat,
      int sync_mask, int local_tree_backup_type);
  static void SetSyncFileMask(
      const FileStat *local_file_stat, const FileStat *remote_file_stat,
      int *sync_mask);
  static bool HandleLocalFileConsistent(
    const Tree &local_tree, const Tree &remote_tree, int32_t sync_perm, 
    std::unique_ptr<SyncFile> *sync_file);
  static bool IsFileStatConsistent(
      const std::string &path, const FileStat *file_stat);

  virtual bool Evaluate(ContentOperation *cp);
  virtual void set_tmp_file_path(const std::string &tmp_file_path) {
    tmp_file_path_ = tmp_file_path;
  }

  bool forbid_history_from_super_;
 protected:
  FileStat *local_file_stat_, *remote_file_stat_;
  std::string local_tree_root_, remote_tree_root_;
  const Uri *uri_;
  int mask_;
  std::string tmp_file_path_;
  int32_t remote_device_;
  std::string remote_device_name_;
  int32_t local_backup_type_;
  int32_t remote_backup_type_;
 private:
  SyncFile(const SyncFile&);
  void operator=(SyncFile&);
  static SyncFile* Create(
      const Tree &local_tree, const Tree &remote_tree, 
      const Device *remote_device, int sync_file_mask);
};

/*  merge the vclcok, update the FileStat by remote file stat */
class SyncFileUpdate : public SyncFile {
 public:
  SyncFileUpdate() {}
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);

 private:
  SyncFileUpdate(SyncFileUpdate&);
  void operator=(SyncFileUpdate&);
};

class SyncFileFnFrUpdateRenameFrom : public SyncFileUpdate {
 public:
  SyncFileFnFrUpdateRenameFrom() {}
  explicit SyncFileFnFrUpdateRenameFrom(SyncFile *sync_file);
  virtual bool Evaluate(ContentOperation *cp); 
 private:
  SyncFileFnFrUpdateRenameFrom(SyncFileFnFrUpdateRenameFrom&);
  void operator=(SyncFileFnFrUpdateRenameFrom&);
};

class SyncFileRename : public SyncFile {
  friend class LocalFileConsistentHandler;
 public:
  SyncFileRename(SyncFile *sync_file_from, SyncFile *sync_file_to):
      sync_file_from_(sync_file_from), sync_file_to_(sync_file_to) {
        set_mask(SYNC_FILE_RENAME_META);
      }
  virtual void Handle(OperationList *op_list, const char *tmp_root = NULL);

  std::unique_ptr<SyncFile>* mutable_sync_file_from() {
    return &sync_file_from_;
  }
  std::unique_ptr<SyncFile>* mutable_sync_file_to() {
    return &sync_file_to_;
  }
 private:
  std::unique_ptr<SyncFile> sync_file_from_, sync_file_to_;
};

class LocalFileConsistentHandler {
 public :
  LocalFileConsistentHandler(
      const Tree &local_tree, const Tree &remote_tree,
      const Sync &sync):
      local_tree_(local_tree), remote_tree_(remote_tree), sync_(sync)  {}
  bool Handle(std::unique_ptr<SyncFile> *sync_file);
  /* return false, means has inconsistent. In this case, the function may
   * change the sync_file_from_ and sync_file_to_*/
  bool Handle(std::unique_ptr<SyncFileRename> *sync_file);

 private:
  LocalFileConsistentHandler(LocalFileConsistentHandler&);
  void operator=(LocalFileConsistentHandler&);

  void GenNewSyncFile(std::unique_ptr<SyncFile> *sync_file, int new_sync_mask);
  bool LocalRegRemoteNormalReg(
      std::unique_ptr<SyncFile> *sync_file, const OsFileStat &os_stat);
  bool LocalDirRemoteNormalReg(
      std::unique_ptr<SyncFile> *sync_file, const OsFileStat &os_stat);
  bool LocalRegRemoteNormalDir(
      std::unique_ptr<SyncFile> *sync_file, const OsFileStat &os_stat);
  bool LocalDirRemoteNormalDir(
      std::unique_ptr<SyncFile> *sync_file, const OsFileStat &os_stat);
  bool RemoteRemove(
      std::unique_ptr<SyncFile> *sync_file, const OsFileStat &os_stat);
  void UpdateLocalFileStat(
      FileStat *local_file_stat, const OsFileStat &os_stat);
  void UpdateLocalFileStatBySyncMask(
      FileStat *local_file_stat, int sync_mask);
  bool LocalFileSha1Changed(const OsFileStat &os_stat);
  bool HandleRenameFrom(std::unique_ptr<SyncFile> *sync_file_from);
  bool HandleRenameTo(std::unique_ptr<SyncFile> *sync_file_from);

  const Tree &local_tree_, &remote_tree_; 
  const Sync &sync_;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_WORKER_SYNC_FILE_H_
