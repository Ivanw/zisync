// Copyright 2014, zisync.com

#include <cstring>
#include <string>
#include <vector>
#ifdef _MSC_VER
  #pragma warning( push )
  #pragma warning( disable : 4244)
  #pragma warning( disable : 4267)
  #include "zisync/kernel/proto/kernel.pb.h"
  #pragma warning( pop )
#else
  #include "zisync/kernel/proto/kernel.pb.h"
#endif

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/utils/vector_clock.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/database/icore.h"

namespace zs {

using std::vector;
using std::string;
  
VectorClock::VectorClock(const void *blob_base, int32_t blob_size) {
  len = blob_size / sizeof(int32_t);
  data = new int32_t[len];
  memcpy(data, blob_base, blob_size);
}
  
VectorClock::VectorClock(const MsgStat &find_stat, 
                         const std::vector<int> &remote_map_to_local, int len_) {
  len = len_;
  data = new int32_t[len];
  memset(data, 0, sizeof(int32_t) * len);
  for (int i = 0; i < find_stat.vclock_size(); i ++) {
    // ZSLOG_ERROR("remote_map_to_local[%d] = %d, len = %d", i, remote_map_to_local[i], len);
    assert(remote_map_to_local[i] < len);
    data[remote_map_to_local[i]] = find_stat.vclock(i);
  }
}
VectorClock::VectorClock(
    int local_vclock, const void* remote_blob_base, int32_t remote_blob_size) {
  len = remote_blob_size / sizeof(int32_t) + 1;
  data = new int32_t[len];
  memcpy(data + 1, remote_blob_base, remote_blob_size);
  data[0] = local_vclock;
}
  
VectorClock::VectorClock(int local_vclock, 
              const void* remote_blob_base, int32_t remote_blob_size,
              const std::vector<int> &map, int vclock_len) {
  len = vclock_len;
  data = new int32_t[len];
  const int32_t *remote_vclock = static_cast<const int32_t*>(remote_blob_base);
  memset(data, 0, sizeof(int32_t) * len);
  assert(map[0] < len);
  data[map[0]] = local_vclock;
  for (unsigned int i = 1; i < remote_blob_size / sizeof(int32_t) + 1; i ++) {
    assert(map[i] < len);
    assert(i < map.size());
    data[map[i]] = remote_vclock[i - 1];
  }
}

VClockCmpResult VectorClock::Compare(const VectorClock &clock) const {
  int compare_len = len > clock.len ? len : clock.len;
  VClockCmpResult result = VCLOCK_EQUAL;
  for (int i = 0; i < compare_len; i ++) {
    VClockCmpResult loop_result;
    int32_t local_clock = i >= len ? 0 : this->at(i);
    int32_t remote_clock = i >= clock.len ? 0 : clock.at(i);

    if (local_clock < remote_clock) {
      loop_result = VCLOCK_LESS;
    } else if (local_clock  == remote_clock) {
      loop_result = VCLOCK_EQUAL;
    } else {
      loop_result = VCLOCK_GREATER;
    }

    if (result == VCLOCK_EQUAL) {
      result = loop_result;
    } else if (loop_result == VCLOCK_EQUAL) {
    } else if (result != loop_result) {
      return VCLOCK_CONFLICT;
    }
  }
  return result;
}

void VectorClock::Merge(const VectorClock &clock) {
  assert(data != NULL);
  if (len < clock.len) {
    int32_t *new_data = new int32_t[clock.len];
    memcpy(new_data, data, sizeof(int32_t) * len);
    memset(&new_data[len], 0, sizeof(int32_t) * (clock.len - len));
    delete data;
    data = new_data;
    len = clock.len;
  }
   
  for (int i = 0; i < len; i ++) {
    int32_t remote_clock = i >= clock.len ? 0 : clock.at(i);
    data[i] = data[i] > remote_clock ? data[i] : remote_clock;
  }
}

VectorClock::VectorClock(const VectorClock& clock) {
  data = new int32_t[clock.len];
  len = clock.len;
  memcpy(data, clock.data, sizeof(int32_t) * len);
}

string VectorClock::ToString() const {
  string str;
  str += "[";
  for (int i = 0; i < len - 1; i ++) {
    StringAppendFormat(&str, "%" PRId32 ", ", data[i]);
  }
  StringAppendFormat(&str, "%" PRId32 "]", data[len - 1]);
  return str;
}

}  // namespace zs
