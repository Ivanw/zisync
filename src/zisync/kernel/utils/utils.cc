// Copyright 2014, zisync.com
#include <vector>
#include <memory>
#include <cstdlib>

#include "zisync/kernel/utils/utils.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/sync.h"

namespace zs {

using std::vector;
using std::unique_ptr;

bool IsSystemRootDir(const string &path) {
  if (GetPlatform() == PLATFORM_WINDOWS) {
    return (path.length() == 3) && (path.substr(1) == ":/");
  } else {
    return path == "/";
  }
}

err_t GenDeviceRootForBackup(
    const char *device_name, string *device_backup_root) {
  if (Config::backup_root().size() == 0) {
    ZSLOG_ERROR("Config::backup_root is empty");
    return ZISYNC_ERROR_CONFIG;
  }
  StringFormat(device_backup_root, "%s/%s", 
               Config::backup_root().c_str(), device_name);
  return ZISYNC_SUCCESS;
}

bool HasMtimeChanged(int64_t mtime_in_fs, int64_t mtime_in_db) {
  Platform platform = GetPlatform();
  if (platform == PLATFORM_ANDROID || platform == PLATFORM_IOS) {
    return false;
  } else {
    return mtime_in_fs != mtime_in_db;
  }
}

bool HasAttrChanged(int32_t attr_in_fs, int32_t attr_in_db) {
  if (GetPlatform() == PLATFORM_ANDROID) {
    return false;
  } else {
    return attr_in_fs != attr_in_db;
  }
}

void GenConflictFilePath(
    string *conflict_file_path, const string &tree_root, 
    const string &file_path) {
  size_t pos = file_path.find_last_of("/.");
  assert(pos != string::npos);
  string first_part, second_part;
  bool has_suffix;

  if (file_path[pos] == '/') {
    // has no suffix
    has_suffix = false;
  } else {
    // for /test/a.text
    has_suffix = true;
    first_part = file_path.substr(0, pos); // /test/a
    second_part = file_path.substr(pos + 1); // .txt
  }
  int i = 0;

  while (true) {
    if (!has_suffix) {
      if (i == 0) {
        StringFormat(conflict_file_path, "%s.conflict", 
                     file_path.c_str());
        // StringFormat(conflict_file_path, "%s.conflict(%s)", 
        //              conflict_desc.c_str(), file_path.c_str());
      } else {
        StringFormat(conflict_file_path, "%s.conflict.%d", 
                     file_path.c_str(), i);
        // StringFormat(conflict_file_path, "%s.conflict(%s).%d", 
        //              conflict_desc.c_str(), file_path.c_str(), i);
      }
    } else {
      if (i == 0) {
        StringFormat(conflict_file_path, "%s.conflict.%s", 
                     first_part.c_str(), second_part.c_str());
        // StringFormat(conflict_file_path, "%s.conflict(%s).%s", 
        //              first_part.c_str(),
        //              conflict_desc.c_str(), second_part.c_str());
      } else {
        StringFormat(conflict_file_path, "%s.conflict.%d.%s", 
                     first_part.c_str(), i, second_part.c_str());
        // StringFormat(conflict_file_path, "%s.conflict(%s).%d.%s", 
        //              first_part.c_str(), conflict_desc.c_str(), 
        //              i, second_part.c_str());
      }
    }
    if (!OsExists(tree_root + *conflict_file_path)) {
      return;
    }
    i ++;
  }
}

}  // namespace zs
