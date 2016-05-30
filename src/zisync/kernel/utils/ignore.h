// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_UTILS_IGNORE_FILE_H_
#define ZISYNC_KERNEL_UTILS_IGNORE_FILE_H_
#include <string>

namespace zs {
/**
 * @param path: absolute path
 */
bool IsIgnoreFile(const std::string &path);
/**
 * @param path: absolute path
 */
bool IsIgnoreDir(const std::string &path);
/**
 * @param path: relative path to tree root
 */
bool IsInIgnoreDir(const std::string &path);
}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_IGNORE_FILE_H_

