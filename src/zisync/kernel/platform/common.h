// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_PLATFORM_COMMON_H_
#define ZISYNC_KERNEL_PLATFORM_COMMON_H_

#include <cstdint>
#include <string>
#include <iostream>

namespace zs {

using std::string;

class IRunnable {
 public:
    virtual ~IRunnable() { /* virtual destructor */ }
    virtual int Run() = 0;
};

class IAbortable {
 public:
    virtual ~IAbortable() { /* virtual destructor */ }
    virtual bool Abort()     = 0;
    virtual bool IsAborted() = 0;
};

class IHandler {
 public:
  virtual ~IHandler() { /* virtual destructor */ }

    virtual int Post(IRunnable* runable) = 0;
};

enum OsFileType {
    OS_FILE_TYPE_REG   = 1,
    OS_FILE_TYPE_DIR   = 2,
};

/* @TODO other platform please give the right value */
const int32_t DEFAULT_WIN_REG_ATTR = 0, DEFAULT_WIN_DIR_ATTR = 16,
      DEFAULT_UNIX_REG_ATTR = 0644, DEFAULT_UNIX_DIR_ATTR = 0775,
      DEFAULT_ANDROID_REG_ATTR = 0644, DEFAULT_ANDROID_DIR_ATTR = 0775;

class OsFileStat {
 public:
  OsFileStat():type(OS_FILE_TYPE_REG), mtime(-1), length(-1), 
    attr(-1) {}

  OsFileType    type;
  int64_t       mtime;
  int64_t       length;
  int32_t       attr;
  string        path;
  string        alias;
};

/* if NOENT return NOENT, else, success return 0, fial return -1 */
int OsStat(const string& path, const string& alias, OsFileStat* file_stat);

class IFsVisitor {
  public:
   virtual ~IFsVisitor() {}

   virtual int Visit(const OsFileStat &stat) = 0;
   virtual bool IsIgnored(const string &path) const = 0;
    
};

enum Platform {
  PLATFORM_MAC       = 1,
  PLATFORM_LINUX     = 2,
  PLATFORM_WINDOWS   = 3,
  PLATFORM_ANDROID   = 4,
  PLATFORM_IOS       = 5,
};

/* File System Functions */
/*  if noent , still return success */
int  OsDeleteFile(const char* path, bool move_to_trash);
inline int OsDeleteFile(const string& path, bool move_to_trash) {
  return OsDeleteFile(path.c_str(), move_to_trash);
}
/*  if dir exists still return success
 *  if the parent dirs do not exist, recursive create */
int  OsCreateDirectory(const char* path, bool delete_file_existing);
inline int  OsCreateDirectory(
    const string& path, bool delete_file_existing) {
  return OsCreateDirectory(path.c_str(), delete_file_existing);
}

/*  exist, no matter file or dir */
bool OsExists(const char* path);
inline bool OsExists(const string& path) {
  return OsExists(path.c_str());
}
/* exist and is a file */
bool OsFileExists(const char* path);
inline bool OsFileExists(const string& path) {
  return OsFileExists(path.c_str());
}
/* exist and is a dir */
bool OsDirExists(const char *path);
inline bool OsDirExists(const string& path) {
  return OsDirExists(path.c_str());
}

/*  if dir does not exist still return success, 
 *  if this is a soft link in linux, remove it*/
int OsDeleteDirectory(const char* path);
inline int OsDeleteDirectory(const string& path) {
  return OsDeleteDirectory(path.c_str());
}
/*  recursive delete */
int OsDeleteDirectories(const char* path, bool delete_self = true);
inline int OsDeleteDirectories(
    const string& path, bool delete_self = true) {
  return OsDeleteDirectories(path.c_str(), delete_self);
}

int OsSetMtime(const char *path, int64_t mtime_in_ms);
inline int OsSetMtime(const string &path, int64_t mtime_in_ms) {
  return OsSetMtime(path.c_str(), mtime_in_ms);
}
int OsChmod(const char *path, int32_t attr);
inline int OsChmod(const string &path, int32_t attr) {
  return OsChmod(path.c_str(), attr);
}
  
/* if the parent dir of dst not exist, should make it */
int OsRename(const char *src, const char *dst, bool is_overwrite = true);
inline int OsRename(const string &src, const string &dst, 
                    bool is_overwrite = true) {
#ifndef NDEBUG
  if (dst.find("conflict") != std::string::npos) {
    std::cout << "Conflict happened, moving " << src << " to " << dst << std::endl;
  }
#endif
  return OsRename(src.c_str(), dst.c_str(), is_overwrite);
}

int OsPathAppend(char* path1, int path1_capacity, const char* path2);
int OsPathAppend(string* path1, const string& path2);

int OsGetFullPath(const char* relative_path, std::string* fullpath);
inline int OsGetFullPath(const std::string& relative_path,
                        std::string* fullpath) {
  return OsGetFullPath(relative_path.c_str(), fullpath);
}

int64_t OsTimeInS();  
int64_t OsTimeInMs();  

int OsGetThreadId();
bool OsGetTimeString(char* buffer, int buffer_length);

/* return true is success, otherwise false */
int OsGetHostname(string* hostname);
/* the return buffer should be in static memory */
const char* OsGetLastErr();
/*  success 0, fail -1 */
void OsGenUuid(string *uuid);
/* return true is success, otherwise false */
bool OsTempPath(const string& dir, const string& prefix, string* tmp_path);
/* get a random available Tcp port, if fail return -1 */
int32_t OsGetRandomTcpPort();
int OsAddHiddenAttr(const char *path);
std::string OsDirName(const std::string& path); 
std::string OsGetMacAddress();

}  // namespace zs 


#endif  // ZISYNC_KERNEL_PLATFORM_COMMON_H_
