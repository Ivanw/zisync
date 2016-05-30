// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_NORMALIZE_PATH_H_
#define ZISYNC_KERNEL_NORMALIZE_PATH_H_

#include <string>

namespace zs {

char *normalize_path_for_linux(char *);
char *normalize_path_for_window(char *);

bool NormalizePath(std::string *path);
bool IsAbsolutePath(std::string &path);
bool IsRelativePath(std::string &path);

}  // namespace zs

#endif // ZISYNC_KERNEL_NORMALIZE_PATH_H_
