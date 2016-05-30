/*=============================================================================
#     FileName: base64.h
#         Desc: 
#       Author: PangHai
#        Email: pangzhende@163.com

#      Version: 0.0.1
#   LastChange: 2015-04-03 04:21:37
#      History:
=============================================================================*/

#ifndef ZISYNC_KERNEL_UTILS_BASE64_H_
#define ZISYNC_KERNEL_UTILS_BASE64_H_

#include <string>
namespace zs {

std::string base64_encode(
    unsigned char const* bytes_to_encode, unsigned int in_len);

std::string base64_decode(std::string const& encoded_string);
}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_BASE64_H_
