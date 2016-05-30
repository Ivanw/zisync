#ifndef ZISYNC_KERNEL_UTILS_URL_H_
#define ZISYNC_KERNEL_UTILS_URL_H_

#include <string>

namespace zs {

std::string UrlEncode(const std::string &str);
std::string UrlDecode(const std::string &str);
void GenFixedStringForHttpUri(std::string *uri);

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_URL_H_
