/****************************************************************************
 *       Filename:  tar_helper.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  12/08/14 17:19:45
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Pang Hai 
 *	    Email:  pangzhende@163.com
 *        Company:  
 ***************************************************************************/

#ifndef ZISYNC_KERNEL_TRANSFER_TAR_HELPER_H_
#define ZISYNC_KERNEL_TRANSFER_TAR_HELPER_H_

#include <libtar.h>

#include "zisync/kernel/status.h"

namespace zs {

class TarCloseHelper {
 public:
  TarCloseHelper(TAR* tar) : tar_(tar) {
  }
  ~TarCloseHelper() {
    if (tar_ != NULL) {
      tar_close_raw(tar_);
    }
  }

 private:
  TAR* tar_;
};

class StartSpeedHelper {
 public:
  StartSpeedHelper() {
    err_t ret = GetTreeManager()->StartupTimer();
    assert(ret == ZISYNC_SUCCESS);
  }
  ~StartSpeedHelper() {
      GetTreeManager()->ShutdownTimer();
  }
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_TRANSFER_TAR_HELPER_H_
