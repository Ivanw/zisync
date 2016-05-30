// Copyright 2014 zisync.com

#ifndef ZISYNC_KERNEL_UTILS_CONFIGURE_H_
#define ZISYNC_KERNEL_UTILS_CONFIGURE_H_

#include <string>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"

namespace zs {

using std::string;

class Config {
 public:
  static void SetHomeDir(const char *home_dir);
  // static void SetLogDir(const char *log_dir) {
  //   config.log_dir_.assign(log_dir);
  // }
  //
  // static void SetAppDataDir(const char *database_dir) {
  //   config.database_dir_.assign(database_dir);
  // }

  static const string home_dir();
  static const string database_dir();
  static const string download_cache_dir();
  static const string device_name();
  static const string device_uuid();
  static const string account_name();
  static const string account_passwd();
  static const string account_key();
  static const string token_sha1();
  static int32_t discover_port();
  static int32_t route_port();
  static int32_t data_port();
  static int refresh_workers_num();
  static int sync_workers_num();
  static int outer_workers_num();
  static int inner_workers_num();
  static int sync_interval();
  static int32_t version();
  static void set_discover_port(int32_t discover_port);
  static void set_route_port(int32_t route_port);
  static void set_data_port(int32_t data_port);
  static void set_device_name(const string &device_name);
  static void set_device_uuid(const string &device_uuid);
  static void set_account_name(const string &account_name);
  static void set_account_passwd(const string &account_passwd);
  static void set_sync_interval(int sync_interval);
  static void set_backup_root(const string& backup_root);
  static const string backup_root();
  static void set_tree_root_prefix(const string& tree_root_prefix);
  static const string tree_root_prefix();
  static int64_t set_download_cache_volume(int64_t volume);
  static int64_t download_cache_volume();
  static void set_report_host(const std::string &report_host);
  static std::string report_host();
  static void set_ca_cert(const std::string &ca_cert);
  static std::string ca_cert();
  static void set_mac_token(const std::string &mac_token);
  static std::string mac_token();

#ifdef ZS_TEST
  static void enable_monitor();
  static void disable_monitor();
  static bool is_monitor_enabled();
  static void enable_dht_announce();
  static void disable_dht_announce();
  static bool is_dht_announce_enabled();
  static void enable_broadcast();
  static void disable_broadcast();
  static bool is_broadcast_enabled();
  static void enable_auto_sync();
  static void disable_auto_sync();
  static bool is_auto_sync_enabled();
  static void enable_device_info();
  static void disable_device_info();
  static bool is_device_info_enabled();
  static void enable_push_device_info();
  static void disable_push_device_info();
  static bool is_push_device_info_enabled();
  static void set_test_platform();
  static void set_test_platform(Platform platform);
  static Platform test_platform();
  static bool is_set_test_platform();
  static void clear_test_platform();
  static void enable_announce_token_changed();
  static void disable_announce_token_changed();
  static bool is_announce_token_changed_enabled();
  static void enable_refresh();
  static void disable_refresh();
  static bool is_refresh_enabled();
  static void enable_default_perms();
  static void disable_default_perms();
  static bool is_default_perms_enabled();
#endif

  static err_t ReadFromContent();
  static err_t WriteToContent();

  static const int32_t DefaultDiscoverPort;
  static const int32_t DefaultRoutePort;
  static const int32_t DefaultDataPort;

 private:
  Config();
  Config(const Config&);
  void operator=(const Config&);

  string home_dir_, database_dir_, download_cache_dir_, device_name_,
         device_uuid_, account_name_, account_passwd_, account_key_,
         backup_root_, tree_root_prefix_, token_sha1_, report_host_,
         ca_cert_, mac_token_;
  int64_t download_cache_volume_;
  int32_t discover_port_, route_port_, data_port_;
  int refresh_workers_num_, sync_workers_num_, outer_workers_num_,
      inner_workers_num_;
  int sync_interval_;
  int32_t version_;
#ifdef ZS_TEST
  int masks_;
  Platform platform_;
#endif
  Mutex mutex;

  static Config config;
};

}  // namespace zs

#endif  // ZISYNC_KERNEL_UTILS_CONFIG_H_
