// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_FILE_STAT_H_
#define ZISYNC_KERNEL_UTILS_FILE_STAT_H_

#include <cstdint>
#include <string>
#include <vector>
#include "zisync/kernel/utils/vector_clock.h"

namespace zs {

class OsFileStat;
class ICursor2;
class MsgStat;

class FileStat {
 public:
  explicit FileStat(ICursor2 *cursor);
  FileStat(ICursor2 *cursor, bool /* has_remote_vclock = ture */);
  FileStat(ICursor2 *cursor, bool /* has_remote_vclock = ture */, 
           const std::vector<int> &map, int vclock_len);
  FileStat(const OsFileStat &os_stat, const std::string &tree_root);
  FileStat(const MsgStat &msg_stat, 
                   std::vector<int> &vclock_remote_map_to_local, int len);
  int32_t id;
  int32_t type, status; // use the value in database
  int64_t mtime, length, usn;
  std::string sha1;
  int32_t platform_attr;
  int32_t unix_attr, android_attr, win_attr;
  VectorClock vclock; // thw whole vector clock, index(0) is local
  int32_t local_vclock;
  std::string alias;
  std::string modifier;
  int64_t time_stamp;
  const char* path() const { return path_.c_str(); }

  static const char *file_projs_without_remote_vclock[],
               *file_projs_with_remote_vclock[];
  static const int file_projs_without_remote_vclock_len,
               file_projs_with_remote_vclock_len;
 
 private:
  FileStat(FileStat&);
  void operator=(FileStat&);
  void SetPlatformAttr();
  
  std::string path_; // the absolute path
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_FILE_STAT_H_

