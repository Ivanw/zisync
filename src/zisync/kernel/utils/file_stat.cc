// Copyright 2014, zisync.com

#include "zisync/kernel/utils/file_stat.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/database/table.h"
// #include "zisync/kernel/proto/kernel.pb.h"
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/transfer.h"

namespace zs {

void FileStat::SetPlatformAttr() {
  switch (GetPlatform()) {
    case PLATFORM_WINDOWS:
      platform_attr = win_attr;
      break;
    case PLATFORM_ANDROID:
      platform_attr = android_attr;
      break;
    case PLATFORM_IOS:
    case PLATFORM_LINUX:
    case PLATFORM_MAC:
      platform_attr = unix_attr;
      break;
    default:
      assert(false);
      break;
  }
}

FileStat::FileStat(ICursor2 *cursor):
    id(cursor->GetInt32(0)),
    type(cursor->GetInt32(2)), status(cursor->GetInt32(3)), 
    mtime(cursor->GetInt64(4)), length(cursor->GetInt64(5)), 
    usn(cursor->GetInt64(6)), sha1(cursor->GetString(7)), 
    unix_attr(cursor->GetInt32(8)), android_attr(cursor->GetInt32(9)), 
    win_attr(cursor->GetInt32(10)), 
    local_vclock(cursor->GetInt32(12)),
    time_stamp(cursor->GetInt64(14)),
    path_(cursor->GetString(1)){
      const char *alias_ = cursor->GetString(11); 
      if (alias_ != NULL) {
        alias = alias_;
      }
      const char *mod = cursor->GetString(13);
      if (mod) {
        modifier = mod;
      }
      SetPlatformAttr();
    }

FileStat::FileStat(ICursor2 *cursor, bool /* has_remote_vclock = true */):
    id(cursor->GetInt32(0)),
    type(cursor->GetInt32(2)), status(cursor->GetInt32(3)), 
    mtime(cursor->GetInt64(4)), length(cursor->GetInt64(5)), 
    usn(cursor->GetInt64(6)), sha1(cursor->GetString(7)), 
    unix_attr(cursor->GetInt32(8)), android_attr(cursor->GetInt32(9)), 
    win_attr(cursor->GetInt32(10)),  
    vclock(cursor->GetInt32(12), cursor->GetBlobBase(13), cursor->GetBlobSize(13)),
    local_vclock(cursor->GetInt32(12)),
    time_stamp(cursor->GetInt64(15)), path_(cursor->GetString(1)){
      const char *alias_ = cursor->GetString(11); 
      if (alias_ != NULL) {
        alias = alias_;
      }
      const char *mod = cursor->GetString(14);
      if (mod) {
        modifier = mod;
      }
      SetPlatformAttr();
    }

FileStat::FileStat(ICursor2 *cursor, bool /* has_remote_vclock = ture */, 
           const std::vector<int> &map, int vclock_len):
    id(cursor->GetInt32(0)),
    type(cursor->GetInt32(2)), status(cursor->GetInt32(3)), 
    mtime(cursor->GetInt64(4)), length(cursor->GetInt64(5)), 
    usn(cursor->GetInt64(6)), sha1(cursor->GetString(7)), 
    unix_attr(cursor->GetInt32(8)), android_attr(cursor->GetInt32(9)), 
    win_attr(cursor->GetInt32(10)),  
    vclock(cursor->GetInt32(12), cursor->GetBlobBase(13), cursor->GetBlobSize(13), 
           map, vclock_len),
    local_vclock(cursor->GetInt32(12)), 
    time_stamp(cursor->GetInt64(15)),
    path_(cursor->GetString(1))  {
      const char *alias_ = cursor->GetString(11); 
      if (alias_ != NULL) {
        alias = alias_;
      }
      const char *mod = cursor->GetString(14);
      if (mod) {
        modifier = mod;
      }
      SetPlatformAttr();
    }
  
FileStat::FileStat(const OsFileStat &os_stat, const std::string &tree_root):
    id(-1), type(os_stat.type), 
    status(TableFile::STATUS_NORMAL), mtime(os_stat.mtime),
    length(os_stat.length), usn(-1), platform_attr(os_stat.attr), 
    local_vclock(1), alias(os_stat.alias),
    path_(&*os_stat.path.begin() + tree_root.length()) {
      if (os_stat.type == OS_FILE_TYPE_DIR) {
        win_attr = DEFAULT_WIN_DIR_ATTR;
        unix_attr = DEFAULT_UNIX_DIR_ATTR;
        android_attr = DEFAULT_ANDROID_DIR_ATTR;
      } else if (os_stat.type == OS_FILE_TYPE_REG) {
        win_attr = DEFAULT_WIN_REG_ATTR;
        unix_attr = DEFAULT_UNIX_REG_ATTR;
        android_attr = DEFAULT_ANDROID_REG_ATTR;
      } else {
        assert(false);
      }
      switch (GetPlatform()) {
        case PLATFORM_WINDOWS:
          win_attr = os_stat.attr;
          break;
        case PLATFORM_ANDROID:
          android_attr = os_stat.attr;
          break;
        case PLATFORM_LINUX:
        case PLATFORM_MAC:
        case PLATFORM_IOS:
          unix_attr = os_stat.attr;
          break;
        default:
          assert(false);
          break;
      }
    }

FileStat::FileStat(const MsgStat &msg_stat, 
        std::vector<int> &vclock_remote_map_to_local, int len)://todo set modifier and time_stamp
    id(-1), 
    type(msg_stat.type() == FT_REG ? 
            zs::OS_FILE_TYPE_REG : zs::OS_FILE_TYPE_DIR), 
    status(msg_stat.status() == FS_NORMAL ?
            TableFile::STATUS_NORMAL : TableFile::STATUS_REMOVE),
    mtime(msg_stat.mtime()), length(msg_stat.length()), 
    usn(msg_stat.usn()), sha1(msg_stat.sha1()), 
    unix_attr(msg_stat.unix_attr()), 
    android_attr(msg_stat.android_attr()), 
    win_attr(msg_stat.win_attr()), 
    vclock(msg_stat, vclock_remote_map_to_local, len), 
    local_vclock(vclock.at(0)), modifier(msg_stat.modifier()), 
    time_stamp(msg_stat.time_stamp()), path_(msg_stat.path()) {
        SetPlatformAttr();
    }

const char* FileStat::file_projs_without_remote_vclock[] = {
  TableFile::COLUMN_ID, TableFile::COLUMN_PATH, TableFile::COLUMN_TYPE, 
  TableFile::COLUMN_STATUS, TableFile::COLUMN_MTIME, 
  TableFile::COLUMN_LENGTH, TableFile::COLUMN_USN, 
  TableFile::COLUMN_SHA1, TableFile::COLUMN_UNIX_ATTR, 
  TableFile::COLUMN_ANDROID_ATTR, 
  TableFile::COLUMN_WIN_ATTR,  TableFile::COLUMN_ALIAS,
  TableFile::COLUMN_LOCAL_VCLOCK, TableFile::COLUMN_MODIFIER,
  TableFile::COLUMN_TIME_STAMP,
};

const char *FileStat::file_projs_with_remote_vclock[] = {
  TableFile::COLUMN_ID, TableFile::COLUMN_PATH, TableFile::COLUMN_TYPE, 
  TableFile::COLUMN_STATUS, TableFile::COLUMN_MTIME, 
  TableFile::COLUMN_LENGTH, TableFile::COLUMN_USN, 
  TableFile::COLUMN_SHA1, TableFile::COLUMN_UNIX_ATTR, 
  TableFile::COLUMN_ANDROID_ATTR, 
  TableFile::COLUMN_WIN_ATTR, TableFile::COLUMN_ALIAS,
  TableFile::COLUMN_LOCAL_VCLOCK, TableFile::COLUMN_REMOTE_VCLOCK,
  TableFile::COLUMN_MODIFIER, TableFile::COLUMN_TIME_STAMP,
};

const int FileStat::file_projs_without_remote_vclock_len = 
ARRAY_SIZE(file_projs_without_remote_vclock);
const int FileStat::file_projs_with_remote_vclock_len = 
ARRAY_SIZE(file_projs_with_remote_vclock);


}  // namespace zs
