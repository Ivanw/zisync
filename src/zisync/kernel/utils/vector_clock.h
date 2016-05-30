// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_VECTOR_CLOCK_H_
#define ZISYNC_KERNEL_UTILS_VECTOR_CLOCK_H_

#include <cstdint>
#include <cassert>
#include <vector>
#include <cstdlib>
#include <string>

namespace zs {

class MsgStat;

enum VClockCmpResult {
  VCLOCK_EQUAL    = 0,
  VCLOCK_LESS     = 1,
  VCLOCK_GREATER  = 2,
  VCLOCK_CONFLICT = 3,
};

class VectorClock {
 public:
  VectorClock():data(NULL), len(0) {}
  VectorClock(const MsgStat &find_stat, 
              const std::vector<int> &remote_map_to_local, int len);
  VectorClock(const VectorClock& clock);
  VectorClock(int local_vclock, 
              const void* remote_blob_base, int32_t remote_blob_size);
  VectorClock(int local_vclock, 
              const void* remote_blob_base, int32_t remote_blob_size,
              const std::vector<int> &map, int vclock_len);
  ~VectorClock() {
    delete[] data;
  }
  VectorClock(const void *blob_base, int32_t blob_size);
  VClockCmpResult Compare(const VectorClock &clock) const;
  void Merge(const VectorClock &clock);

  int length() const { return len; }
  int32_t& at(int index) const { assert(index < len); return data[index]; }
  int32_t* remote_vclock() const { return &data[1]; }
  int32_t remote_vclock_size() const { return (len - 1) * sizeof(int32_t); }
  
  std::string ToString() const ;
 private:
  void operator=(VectorClock&);
  
  int32_t *data;
  int len;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_VECTOR_CLOCK_H_
