// Copyright 2014, zisync.com
#include <cstring>
#include <algorithm>

#include "normalize_path.h"
#include "zisync/kernel/platform/platform.h"
#ifdef ZS_TEST
#include "zisync/kernel/utils/configure.h"
#endif

namespace zs {

bool NormalizePath(std::string *path) {
  char *ret;
  Platform platform = GetPlatform();
#ifdef ZS_TEST
  if (Config::is_set_test_platform()) {
    platform = Config::test_platform();
  }
#endif
  if (platform == PLATFORM_WINDOWS) {
    ret = normalize_path_for_window(&(*path->begin()));
    std::replace(path->begin(), path->end(), '\\', '/');
  } else {
    ret = normalize_path_for_linux(&(*path->begin()));
  }
  path->resize(strlen(path->c_str()));
  return ret != NULL;
}

bool IsAbsolutePath(std::string &path) {
  Platform platform = GetPlatform();
#ifdef ZS_TEST
  if (Config::is_set_test_platform()) {
    platform = Config::test_platform();
  }
#endif
  if (platform == PLATFORM_WINDOWS) {
    return (path.length() > 2) && 
        ((path.at(0) >= 'A' && path.at(0) <= 'Z') ||
         (path.at(0) >= 'a' && path.at(0) <= 'z')) &&
        ((path.compare(1, 2, ":/") == 0) || 
         path.compare(1, 2, ":\\") == 0);
  } else {
    return (path.length() > 0) && (path.at(0) == '/');
  }
}

/* alwasy start with / */
bool IsRelativePath(std::string &path) {
  return (path.length() > 0) && ((path.at(0) == '/') || (path.at(0) == '\\'));
}

}  // namespace zs  
