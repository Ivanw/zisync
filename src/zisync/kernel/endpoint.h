// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_ENDPOINT_H_
#define ZISYNC_KERNEL_ENDPOINT_H_

namespace zs {

const char exit_uri[] = "inproc://exit";

const char router_sync_fronter_uri[] = "inproc://sync_fronter";
const char router_sync_backend_uri[] = "inproc://sync_backend";
const char router_refresh_fronter_uri[] = "inproc://refresh_fronter";
const char router_refresh_backend_uri[] = "inproc://refresh_backend";
const char router_outer_backend_uri[] = "inproc://outer_backend";
const char router_inner_fronter_uri[] = "inproc://inner_fronter";
const char router_inner_pull_fronter_uri[] = "inproc://inner_pull_fronter";
const char router_inner_backend_uri[] = "inproc://inner_backend";
const char router_cmd_uri[] = "inproc://router_cmd";

}  // namespace zs

#endif  // ZISYNC_KERNEL_ENDPOINT_H_
