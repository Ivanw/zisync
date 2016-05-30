/****************************************************************************
 *       Filename:  format.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  11/05/14 18:09:51
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Pang Hai 
 *	    Email:  pangzhende@163.com
 *        Company:  
 ***************************************************************************/
#ifndef ZISYNC_KERNEL_FORMAT_H_
#define ZISYNC_KERNEL_FORMAT_H_

#include "zisync/kernel/database/icore.h"
#include <string>

namespace zs {

const char* HumanSpeed(double nbytes, std::string* buffer = NULL);
const char* HumanTime(int32_t seconds, std::string* buffer = NULL);
const char* HumanFileSize(int64_t size, std::string* buffer = NULL);

}  // namespace zs

#endif  // ZISYNC_KERNEL_FORMAT_H_
