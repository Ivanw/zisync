// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTIL_TRANSFER_H_
#define ZISYNC_KERNEL_UTIL_TRANSFER_H_

#ifdef _MSC_VER
  #pragma warning( push )
  #pragma warning( disable : 4244)
  #pragma warning( disable : 4267)
  #include "zisync/kernel/proto/kernel.pb.h"
  #pragma warning( pop )
#else
  #include "zisync/kernel/proto/kernel.pb.h"
#endif

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/common.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/zslog.h"

namespace zs {

inline Platform MsgDeviceTypeToPlatform(MsgDeviceType type) {
  switch (type) {
    case DT_LINUX:
      return PLATFORM_LINUX;
    case DT_ANDROID:
      return PLATFORM_ANDROID;
    case DT_WINDOWS:
      return PLATFORM_WINDOWS;
    case DT_MAC:
      return PLATFORM_MAC;
    default:
	  assert (type == DT_IOS);
      return PLATFORM_IOS;
  }
}

inline int MsgDeviceTypeToDeviceType(MsgDeviceType type) {
  switch (type) {
    case DT_LINUX:
      return DEVICE_TYPE_UNKOWN;
    case DT_ANDROID:
      return DEVICE_TYPE_ANDROID;
    case DT_WINDOWS:
      return DEVICE_TYPE_WINDOWS;
    case DT_MAC:
      return DEVICE_TYPE_MAC;
    default:
	  assert (type == DT_IOS);
      return DEVICE_TYPE_IPHONE;
  }
}

inline MsgDeviceType PlatformToMsgDeviceType(Platform platform) {
  switch (platform) {
    case PLATFORM_LINUX:
      return DT_LINUX;
    case PLATFORM_ANDROID:
      return DT_ANDROID;
    case PLATFORM_WINDOWS:
      return DT_WINDOWS;
    case PLATFORM_MAC:
      return DT_MAC;
    default:
      assert(platform == PLATFORM_IOS);
      return DT_IOS;
  }
}

inline MsgSyncType TableSyncTypeToMsg(int32_t sync_type) {
  if (sync_type == TableSync::TYPE_NORMAL) {
    return ST_NORMAL;
  } else if (sync_type == TableSync::TYPE_BACKUP) {
    return ST_BACKUP;
  } else {
	assert(sync_type == TableSync::TYPE_SHARED);
    return ST_SHARED;
  }
}

inline int32_t MsgSyncTypeToTable(MsgSyncType sync_type) {
  switch (sync_type) {
    case ST_BACKUP:
      return TableSync::TYPE_BACKUP;
    case ST_SHARED:
      return TableSync::TYPE_SHARED;
    default:
	  return TableSync::TYPE_NORMAL;
  }
}

inline FileMetaType OsFileTypeToFileMetaType(OsFileType type) {
  if (type == OS_FILE_TYPE_REG) {
    return FILE_META_TYPE_REG;
  } else {
	assert (type == OS_FILE_TYPE_DIR);
    return FILE_META_TYPE_DIR;
  }
}

inline MsgSyncMode SyncModeToMsgSyncMode(int32_t sync_mode) {
  if (sync_mode == SYNC_MODE_AUTO) {
    return SM_AUTO;
  } else if (sync_mode == SYNC_MODE_TIMER) {
    return SM_TIMER;
  } else {
    assert(sync_mode == SYNC_MODE_MANUAL);
    return SM_MANUAL;
  }
}

inline int32_t MsgSyncModeToSyncMode(MsgSyncMode sync_mode) {
  if (sync_mode == SM_AUTO) {
    return SYNC_MODE_AUTO;
  } else if (sync_mode == SM_TIMER) {
    return SYNC_MODE_TIMER;
  } else {
    assert(sync_mode == SM_MANUAL);
    return SYNC_MODE_MANUAL;
  }
}

inline int32_t MsgSyncPermToSyncPerm(MsgSyncPerm sync_perm) {
  if (sync_perm == SP_RDONLY) {
    return TableSync::PERM_RDONLY;
  } else if (sync_perm == SP_WRONLY) {
    return TableSync::PERM_WRONLY;
  } else if (sync_perm == SP_RDWR) {
    return TableSync::PERM_RDWR;
  } else if (sync_perm == SP_DISCONNECT) {
    return TableSync::PERM_DISCONNECT;
  } else {
    assert(sync_perm == SP_CREATOR_DELETE);
    return TableSync::PERM_CREATOR_DELETE;
  }
}

inline MsgSyncPerm SyncPermToMsgSyncPerm(int32_t sync_perm) {
  if (sync_perm == TableSync::PERM_RDONLY) {
    return SP_RDONLY;
  } else if (sync_perm == TableSync::PERM_WRONLY) {
    return SP_WRONLY;
  } else if (sync_perm == TableSync::PERM_RDWR) {
    return SP_RDWR;
  } else if (sync_perm == TableSync::PERM_DISCONNECT || 
             sync_perm == TableSync::PERM_TOKEN_DIFF) {
    return SP_DISCONNECT;
  } else {
    assert(sync_perm == TableSync::PERM_CREATOR_DELETE);
    return SP_CREATOR_DELETE;
  }
}

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTIL_TRANSFER_H_
