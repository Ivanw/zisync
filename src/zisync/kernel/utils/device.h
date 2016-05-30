// Copyright 2014, zisync.com

#ifndef ZIDEVICE_KERNEL_UTILS_DEVICE_H_
#define ZIDEVICE_KERNEL_UTILS_DEVICE_H_
#include <string>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/common.h"

namespace zs {

class ICursor2;
class MsgDevice;

class Device {
 public:
  err_t Save();

  // static err_t GetNameById(int32_t id, string *name);
  static Device* GetByIdWhereStatusOnline(int32_t id);
  static Device* GetById(int32_t id);
#if defined(__GNUC__) && (__GNUC__ >= 4)
  __attribute__((format(printf, 1, 2)))
  static Device* GetBy(const char *selection, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  static Device* GetBy(_Printf_format_string_ const char *selection, ...); 
# else
  static Device* GetBy(__format_string const char *selection, ...);
# endif /* FORMAT_STRING */
#else
  static Device* GetBy(const char *selection, ...);
#endif
  /* if not exist return false */
  static bool IsMyDevice(int32_t device_id);

  void ToDeviceInfo(DeviceInfo *device_info) const;
  bool HasChanged(const MsgDevice &device) const;
  void ToMsgDevice(MsgDevice *device) const;

  int32_t id() const { return id_; }
  const std::string& uuid() const { return uuid_; }
  int32_t data_port() const { return data_port_; }
  int32_t route_port() const { return route_port_; }
  bool IsMobile() const;
  bool is_online() const { return is_online_; }
  bool is_mine() const { return is_mine_; }
  int32_t version() const { return version_; }

 protected:
  void ParseFromCursor(ICursor2 *cursor);
  void ParseFromConfig(int32_t id);

  static const char *full_projs[];
  int32_t id_;
  std::string uuid_;
  std::string name_;
  int32_t route_port_;
  int32_t data_port_;
  Platform platform_;
  bool is_mine_;
  bool is_online_;
  std::string backup_dst_root_;
  int32_t version_;
};

}  // namespace zs

#endif  // ZIDEVICE_KERNEL_UTILS_DEVICE_H_
