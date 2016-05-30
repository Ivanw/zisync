// Copyright 2014, zisync.com
#include <memory>
#include <cstdarg>

#include "zisync/kernel/utils/device.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/platform.h"
#include "zisync/kernel/utils/transfer.h"
#include "zisync/kernel/utils/configure.h"

namespace zs {

using std::unique_ptr;

// err_t Device::GetNameById(int32_t id, string *name) {
//   IContentResolver *resolver = GetContentResolver();
//   const char *device_projs[] = {
//     TableDevice::COLUMN_NAME,
//   };
//   unique_ptr<ICursor2> device_cursor(resolver->Query(
//           TableDevice::URI, device_projs, ARRAY_SIZE(device_projs), 
//           "%s = %d AND %s = %d", 
//           TableDevice::COLUMN_ID, id,
//           TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE));
//   if (device_cursor->MoveToNext()) {
//     *name = device_cursor->GetString(0);
//     assert(!device_cursor->MoveToNext());
//     return ZISYNC_SUCCESS;
//   } else {
//     return ZISYNC_ERROR_DEVICE_NOENT;
//   }
// }
bool Device::IsMobile() const {
  return IsMobileDevice(platform_);
}

const char* Device::full_projs[] = {
  TableDevice::COLUMN_ID, TableDevice::COLUMN_UUID, 
  TableDevice::COLUMN_NAME, TableDevice::COLUMN_ROUTE_PORT, 
  TableDevice::COLUMN_DATA_PORT, TableDevice::COLUMN_TYPE, 
  TableDevice::COLUMN_IS_MINE, TableDevice::COLUMN_STATUS,
  TableDevice::COLUMN_BACKUP_DST_ROOT, TableDevice::COLUMN_VERSION,
};

Device* Device::GetBy(const char *selection, ...) {
  va_list ap;
  va_start(ap, selection);
  IContentResolver *resolver = GetContentResolver();
  unique_ptr<ICursor2> device_cursor(resolver->vQuery(
          TableDevice::URI, full_projs, ARRAY_SIZE(full_projs), 
          selection, ap));
  va_end(ap);
  if (device_cursor->MoveToNext()) {
    Device *device = new Device;
    device->ParseFromCursor(device_cursor.get());
    return device;
  } else {
    return NULL;
  }
}

Device* Device::GetByIdWhereStatusOnline(int32_t id) {
  if (id == TableDevice::LOCAL_DEVICE_ID) {
    Device *device = new Device;
    device->ParseFromConfig(id);
    return device;
  }
  return GetBy("%s = %d AND %s = %d", 
          TableDevice::COLUMN_ID, id,
          TableDevice::COLUMN_STATUS, TableDevice::STATUS_ONLINE);
}

Device* Device::GetById(int32_t id) {
  if (id == TableDevice::LOCAL_DEVICE_ID) {
    Device *device = new Device;
    device->ParseFromConfig(id);
    return device;
  }
  return GetBy("%s = %d", TableDevice::COLUMN_ID, id);
}

void Device::ToDeviceInfo(DeviceInfo *device_info) const {
  device_info->device_id = id_;
  device_info->device_name = name_;
  device_info->device_type = MsgDeviceTypeToDeviceType(
      PlatformToMsgDeviceType(platform_));
  device_info->is_mine = is_mine_;
  device_info->is_backup = false;
  device_info->is_online = is_online_;
  device_info->is_shared = false;
  device_info->backup_root = backup_dst_root_;
  device_info->version = version_;
}

void Device::ParseFromCursor(ICursor2 *cursor) {
  id_ = cursor->GetInt32(0);
  uuid_ = cursor->GetString(1);
  name_ = cursor->GetString(2);
  route_port_ = cursor->GetInt32(3);
  data_port_ = cursor->GetInt32(4);
  platform_ = static_cast<Platform>(cursor->GetInt32(5));
  is_mine_ = cursor->GetBool(6);
  is_online_ = 
      cursor->GetInt32(7) == TableDevice::STATUS_ONLINE;
  if (cursor->GetString(8) != NULL) {
    backup_dst_root_ = cursor->GetString(8);
  }
  version_ = cursor->GetInt32(9);
}

void Device::ParseFromConfig(int32_t id) {
  id_ = id;
  uuid_ = Config::device_uuid();
  name_ = Config::device_name();
  route_port_ = Config::route_port();
  data_port_ = Config::data_port();
  platform_ = GetPlatform();
  is_mine_ = true;
  is_online_ = true;
  version_ = Config::version();
}

bool Device::HasChanged(const MsgDevice &device) const {
  return (name_ != device.name()) ||  
      (platform_ != MsgDeviceTypeToPlatform(device.type())) ||
      (device.has_version() && (device.version() != version_)) ||
      (data_port_ != device.data_port()) || 
      (route_port_ != device.route_port()) ||
      (device.has_backup_root() && backup_dst_root_ != device.backup_root());
}

bool Device::IsMyDevice(int32_t device_id) {
  IContentResolver *resolver = GetContentResolver();
  const char *device_projs[] = {
    TableDevice::COLUMN_IS_MINE,
  };
  unique_ptr<ICursor2> device_cursor(resolver->Query(
          TableDevice::URI, device_projs, ARRAY_SIZE(device_projs),
          "%s = %d", TableDevice::COLUMN_ID, device_id));
  return device_cursor->MoveToNext() && device_cursor->GetBool(0);
}

void Device::ToMsgDevice(MsgDevice *device) const {
  device->set_type(PlatformToMsgDeviceType(platform_));
  device->set_name(name_);
  device->set_uuid(uuid_);
  device->set_route_port(route_port_);
  device->set_data_port(data_port_);
  device->set_version(version_);
}

}  // namespace zs
