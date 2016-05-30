// Copyright 2014, zisync.com
#include "zisync/kernel/utils/ignore.h"
#include "zisync/kernel/sync_const.h"
  
namespace zs {

using std::string;

static const string ignore_dirs[] = {
  string(PULL_DATA_TEMP_DIR),
  string("$RECYCLE.BIN"),
  string("RECYCLER"),
  string(".thumbnails"),
  string("System Volume Information"),
};

static const string ignore_dirs_in_root[] = {
  string(PULL_DATA_TEMP_DIR),
  string("$RECYCLE.BIN"),
  string("RECYCLER"),
  string(".thumbnails"),
  string("System Volume Information"),
};

static const string ignore_files[] = {
  string(SYNC_FILE_TASKS_META_FILE),
};

bool IsIgnoreFile(const std::string &path) {
  size_t pos = path.find_last_of('/') + 1;
  for (unsigned int i = 0; i < sizeof(ignore_files) / sizeof(string); i ++) {
    if (0 == path.compare(pos, string::npos, ignore_files[i])) {
      return true;
    }
  }
  return false;
}

bool IsIgnoreDir(const std::string &path) {
  size_t pos = path.find_last_of('/') + 1;
  for (unsigned int i = 0; i < sizeof(ignore_dirs) / sizeof(string); i ++) {
    if (i == 0) {
      if (0 == path.compare(pos, ignore_dirs[i].length(), ignore_dirs[i])) {
        return true;
      }
    } else {
      if (0 == path.compare(pos, string::npos, ignore_dirs[i])) {
        return true;
      }
    }
  }
  return false;
}

// yet only can treat the IgnoreDir with is in Root dir
bool IsInIgnoreDir(const std::string &path) {
  for (unsigned int i = 0; i < sizeof(ignore_dirs_in_root) / sizeof(string); i ++) {
    if (i == 0) {
      if (path.compare(1, ignore_dirs_in_root[i].length(), ignore_dirs_in_root[i]) == 0) {
        return true;
      }
    } else  {
      if (path.compare(1, ignore_dirs_in_root[i].length(), ignore_dirs_in_root[i]) == 0 &&
          (path.length() == ignore_dirs_in_root[i].length() + 1 ||
           path.at(ignore_dirs_in_root[i].length() + 1) == '/')) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace zs
