// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_SYNC_H_
#define ZISYNC_KERNEL_UTILS_SYNC_H_
#include <string>
#include <memory>

#include "zisync_kernel.h"  // NOLINT

namespace zs {

using std::string;

class ICursor2;
class MsgSync;

class Sync {
 public:
  Sync() {}
  virtual ~Sync() {}

  err_t Create(const string &sync_name);
  static err_t DeleteById(int32_t sync_id);
  static bool ExistsWhereStatusNormal(int32_t id);
  static Sync* GetByIdWhereStatusNormal(int32_t id);
  static Sync* GetByUuid(const std::string& uuid);
#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 1, 2)))
  static Sync* GetBy(const char *selection, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  static Sync* GetBy(_Printf_format_string_ const char *selection, ...);
# else
  static Sync* GetBy(__format_string const char *selection, ...);
# endif /* FORMAT_STRING */
#else
  static Sync* GetBy(const char *selection, ...);
#endif
  /* ZISYNC_ERROR_SYNC_NOENT or ZISYNC_SUCCESS */
  /*  Query share and normal */
  static void QueryWhereStatusNormalTypeNotBackup(
      std::vector<std::unique_ptr<Sync>> *syncs);

  static int32_t GetIdByUuidWhereStatusNormal(const string &uuid);
  static Sync* GetByIdWhereStatusNormalTypeNotBackup(int32_t id);
  
  static int32_t GetSyncPermByIdWhereStatusNormal(int32_t id);
 
  static bool IsUnusable(int32_t sync_perm);
  static bool IsCreator(int32_t id);
  bool ToSyncInfo(SyncInfo *sync_info) const;
  void ToMsgSync(MsgSync *sync) const;
  bool IsUnusable() const {
    return IsUnusable(perm_);
  }
  int32_t id() const { return id_; }
  const string& uuid() const { return uuid_; }
  const string& name() const { return name_; }
  int64_t last_sync() const { return last_sync_; }
  void set_uuid(const std::string uuid) { uuid_ = uuid; }
  int32_t device_id() const { return device_id_; }
  bool is_normal() const { return is_normal_; }
  virtual int32_t type() const = 0;
  int32_t perm() const { return perm_; }
  int32_t restore_share_perm() const { return restore_share_perm_; }

 protected:
  static Sync* Generate(ICursor2 *cursor);
  void ParseFromCursor(ICursor2 *cursor);
  
  static const char* full_projs[];
  int32_t id_;
  string uuid_;
  string name_;
  int64_t last_sync_;
  bool is_normal_;
  int32_t device_id_;
  int32_t perm_;
  int32_t restore_share_perm_;
};

class Backup : public Sync {
 public:
  Backup() {}
  virtual ~Backup() {}

  err_t Create(const string &name, const string &root);
  bool ToBackupInfo(BackupInfo *backup_info) const ;

  static err_t DeleteById(int32_t id) { return Sync::DeleteById(id); }
  
  static Backup* GetByIdWhereStatusNormal(int32_t id);
  static void QueryWhereStatusNormal(
      std::vector<std::unique_ptr<Backup>> *backups);
  virtual int32_t type() const;
};

class NormalSync : public Sync {
  friend class Sync;
 public:
  NormalSync() {};
  virtual int32_t type() const;
};

class ShareSync : public Sync {
  friend class Sync;
 public:
  ShareSync() {};
  virtual int32_t type() const;
};

int32_t ToExternalSyncType(int32_t type);
}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_SYNC_H_
