/****************************************************************************
 *       Filename:  format.cc
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  11/06/14 10:05:10
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Pang Hai 
 *	    Email:  pangzhende@163.com
 *        Company:  
 ***************************************************************************/
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/format.h"
#include <assert.h>

namespace zs {

const char* HumanSpeed(double nbytes, std::string* buffer) {
  static std::string static_buffer;
  std::string* s = buffer ? buffer : &static_buffer;
  if (nbytes < 1024) {
    StringFormat(s, "%.1lfB/s", nbytes);
    return s->c_str();
  }

  nbytes /= 1024;
  if (nbytes < 1024) {
    StringFormat(s, "%.1lfKB/s", nbytes);
    return s->c_str();
  }

  nbytes /= 1024;
  if (nbytes < 1024) {
    StringFormat(s, "%.1lfMB/s", nbytes);
    return s->c_str();
  }

  nbytes /= 1024;
  if (nbytes < 1024) {
    StringFormat(s, "%.1lfGB/s", nbytes);
    return s->c_str();
  }

  nbytes /= 1024;
  StringFormat(s, "%.1lfTB/s", nbytes);
  assert(false);

  return s->c_str();
}

const char* HumanTime(int32_t seconds, std::string* buffer) {
  static std::string static_buffer;
  std::string* s = buffer ? buffer : &static_buffer;

  if (seconds >= 0) {
    if (seconds < 60) {
      StringFormat(s, "%ds", seconds);
      return s->c_str();
    }

    if (seconds < 3600) {
      StringFormat(s, "%dm%ds", seconds / 60, seconds % 60);
      return s->c_str();
    }

    seconds /= 60;
    if (seconds < 1440) {
      StringFormat(s, "%dh%dm", seconds / 60, seconds % 60);
      return s->c_str();
    }

    seconds /=60;
    if (seconds < 3) {
      StringFormat(s, "%dd%dh", seconds / 24, seconds % 24);
      return s->c_str();
    }
  }

  //  StringFormat(s, "%d, %d", seconds / 24, seconds % 24);
  return s->c_str();
}

const char* HumanFileSize(int64_t size, std::string* buffer) {
  static std::string static_buffer;
  std::string* s = buffer ? buffer : &static_buffer;

  if (size < 1024) {
    StringFormat(s, "%" PRId64, size);
    return s->c_str();
  }

  double dSize = (static_cast<double>(size)) / 1024;
  if (dSize < 1024) {
    StringFormat(s, "%.1lfK", dSize);
    return s->c_str();
  }

  dSize /= 1024;
  if (dSize < 1024) {
    StringFormat(s, "%.1lfM", dSize);
    return s->c_str();
  }

  dSize /= 1024;
  if (dSize < 1024) {
    StringFormat(s, "%.1lfG", dSize);
    return s->c_str();
  }

  dSize /= 1024;
  StringFormat(s, "%.1lfT", dSize);
  assert(false);

  return s->c_str();
}

}  // namespace zs

