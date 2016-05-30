// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_RENAME_H_
#define ZISYNC_KERNEL_UTILS_RENAME_H_

#include <memory>
#include <vector>
#include <map>

#include "zisync/kernel/worker/sync_file.h"

namespace zs {

class RenameHandler {
 public:
  virtual ~RenameHandler() {};
  /* take the own of file_stats */
  virtual void HandleRename(
      SyncFile *from_file_stat, SyncFile *to_file_stat) = 0;
  virtual void HandleRenameFrom(SyncFile *from_file_stat) = 0;
  virtual void HandleRenameTo(SyncFile *from_file_stat) = 0;
};

class Rename {
  friend class RenameManager;
 public:
  Rename(SyncFile *rename_from, SyncFile *rename_to):
      rename_from_(rename_from), rename_to_(rename_to) {}
 private:
  std::unique_ptr<SyncFile> rename_from_, rename_to_;
};

class RenameManager {
 public:
  RenameManager() {}
   /**
    * @return: true: take the own of the file_stat
    *          false : not a rename
    */
  bool AddSyncFile(SyncFile *sync_file);

  void HandleRename(RenameHandler *handler);

  std::map<std::string /* sha1 */, 
      std::vector<std::unique_ptr<SyncFile>>> rename_froms;
  std::map<std::string /* sha1 */, 
      std::vector<std::unique_ptr<SyncFile>>> rename_toes;
  std::vector<std::unique_ptr<Rename>> renames;
 private:
  RenameManager(RenameManager&);
  void operator=(RenameManager&);
};

static inline bool IsRenameFrom(SyncFile *sync_file) {
  return sync_file->mask() == SYNC_FILE_FN_FR_UPDATE_META;
}

static inline bool IsRenameTo(SyncFile *sync_file) {
  return sync_file->mask() == SYNC_FILE_FN_INSERT_DATA || 
      sync_file->mask() == SYNC_FILE_FR_FN_UPDATE_DATA;
}

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_RENAME_H_
