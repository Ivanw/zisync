// Copyright 2014 zisync.com

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"
// #include "zisync/kernel/proto/kernel.pb.h"

namespace zs {

static const char* error_str[] = {
  "Success",
  "Kernel have not startup",
  "General error",
  "Read config fail",
  "Existing Device",
  "Nonexistent Device",
  "Existing Sync",
  "Nonexistent Sync",
  "Existing Tree",
  "Nonexistent Tree",
  "Nested Tree",
  "Nonexistent Directory",
  "Existing Favourite",
  "Nonexistent Favourite",
  "Existing SyncList",
  "Nonexistent SyncList",
  "SyncDir mismatch",
  "Zmq error",
  "Receive message timeout",
  "New fail",
  "Encrypt or decrypt fail",
  "Bad path",
  "OS IO error",
  "Os Socket error",
  "Os Thread error",
  "Os Timer error",
  "Os Event error",
  "Invalid Message",
  "Permission Deny",
  "Content handle error",
  "Sqlite error",
  "Address already in use",
  "Invalid port",
  "Invalid SyncBlob",
  "Invalid uuid",
  "Untrusted",
  "Monitor handle fail",
  "SyncList handle fail",
  "Invalid Method", 
  "LibEvent Error",
  
  "Task abort due to cancel",
  "Put task fail",
  "Tar error",
  "Refused",
  "Get tmp dir fail",
  "Get tree root fail",
  "Invalid format",
  "SSL error",
  "Get certificate fail",
  "Get private key fail",
  "Private key fail",
  "Get CA fail",
  "Invalid Tree ID",
  "Http return error",

  /*  for DiscoverDevice */
  "The limit on the total number of startup discover  has been reached",
  "Nonexistent Discover ID",
  
  /*  for Download */
  "Nonexistent Download task ID",
  "File Noent",

  "Version Incompatible",
  "Invalid path",
  
  "Not a directory",

  "Backup dst exists",
  "Backup src exists",

  "ShareSync Noent",
  "ShareSync is disconnected",
  "Not sync creator",
  "Is sync creator",
  "Sync already has creator",

  "Download file is too large to store in download cache directory",
  "Again",
  "File Exists",
  "Invalid key code",
  "Limit key bind",
  "CDKey error",
  "Bind fail.",
  "Unbind fail.",
  "Verify fail.",
  "Mactoken is not matched.",
  "Tree root moved.",
  "Calc sha1 fail, file may have been exclusively opened by another process."
};

const char* zisync_strerror(err_t err) {
  assert(sizeof(error_str) / sizeof(char*) == ZISYNC_ERROR_NUM);
  assert(err < ZISYNC_ERROR_NUM);
  return error_str[err];
}

}  // namespace zs

