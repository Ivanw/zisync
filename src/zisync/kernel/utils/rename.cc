// Copyright 2014, zisync.com
#include "zisync/kernel/utils/rename.h"
#include "zisync/kernel/utils/file_stat.h"

namespace zs {

using std::vector;
using std::map;
using std::unique_ptr;
using std::string;

bool RenameManager::AddSyncFile(SyncFile *sync_file) {
  if (IsRenameFrom(sync_file)) {
    assert(sync_file->local_file_stat() != NULL);
    const string &sha1 = sync_file->local_file_stat()->sha1;
    auto find = rename_toes.find(sha1);
    if (find == rename_toes.end() || find->second.size() == 0) {
      rename_froms[sync_file->local_file_stat()->sha1].emplace_back(sync_file);
    } else {
      SyncFile *rename_from = new SyncFileFnFrUpdateRenameFrom(sync_file);
      SyncFile *rename_to = find->second.back().release();
      renames.emplace_back(new Rename(rename_from, rename_to));
      find->second.pop_back();
      delete sync_file;
    }
    return true;
  } else if (IsRenameTo(sync_file)) {
    assert(sync_file->remote_file_stat() != NULL);
    const string &sha1 = sync_file->remote_file_stat()->sha1;
    auto find = rename_froms.find(sha1);
    if (find == rename_froms.end() || find->second.size() == 0) {
      rename_toes[sync_file->remote_file_stat()->sha1].emplace_back(sync_file);
    } else {
      SyncFile *rename_from = new SyncFileFnFrUpdateRenameFrom(
          find->second.back().get());
      renames.emplace_back(new Rename(rename_from, sync_file));
      find->second.pop_back();
    }
    return true;
  } else {
    return false;
  }
}

void RenameManager::HandleRename(RenameHandler *handler) {
  for (auto iter1 = rename_froms.begin(); iter1 != rename_froms.end();
       iter1 ++) {
    for (auto iter2 = iter1->second.begin(); iter2 != iter1->second.end();
         iter2 ++) {
      handler->HandleRenameFrom(iter2->release());
    }
  }
  rename_froms.clear();
  
  for (auto iter1 = rename_toes.begin(); iter1 != rename_toes.end();
       iter1 ++) {
    for (auto iter2 = iter1->second.begin(); iter2 != iter1->second.end();
         iter2 ++) {
      handler->HandleRenameTo(iter2->release());
    }
  }
  rename_toes.clear();
  
  for (auto iter = renames.begin(); iter != renames.end(); iter ++) {
    handler->HandleRename(
        (*iter)->rename_from_.release(), (*iter)->rename_to_.release());
  }
  renames.clear();
}

}  // namepace zs
