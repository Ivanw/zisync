// Copyright 2014 zisync.com

#include <memory>
#include <cstring>
#include <stdlib.h>

#include "zisync/kernel/utils/configure.h"
#include "zisync/kernel/database/content_resolver.h"
#include "zisync/kernel/database/table.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/cipher.h"
#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/sha.h"
#include "zisync/kernel/sync_const.h"
#include "zisync/kernel/utils/base64.h"

namespace zs {

const char TokenSha1Salt[] = "zisync";

using std::unique_ptr;

Config Config::config;
  
void Config::SetHomeDir(const char *home_dir) {
  MutexAuto mutex_auto(&config.mutex);
  config.home_dir_.assign(home_dir);

  config.database_dir_.assign(home_dir);
  config.database_dir_ += "/Database";

  config.download_cache_dir_ = config.home_dir_ + "/Cache";
}
const string Config::home_dir() {
  MutexAuto mutex_auto(&config.mutex);
  return config.home_dir_;
}
const string Config::database_dir() {
  MutexAuto mutex_auto(&config.mutex);
  return config.database_dir_;
}
const string Config::download_cache_dir() {
  MutexAuto mutex_auto(&config.mutex);
  return config.download_cache_dir_;
}
const string Config::device_name() {
  MutexAuto mutex_auto(&config.mutex);
  return config.device_name_;
}
const string Config::device_uuid() {
  MutexAuto mutex_auto(&config.mutex);
  return config.device_uuid_;
}
const string Config::account_name() {
  MutexAuto mutex_auto(&config.mutex);
  return config.account_name_;
}
const string Config::token_sha1() {
  MutexAuto mutex_auto(&config.mutex);
  return config.token_sha1_;
}
const string Config::account_passwd() {
  MutexAuto mutex_auto(&config.mutex);
  return config.account_passwd_;
}
const string Config::account_key() {
  MutexAuto mutex_auto(&config.mutex);
  return config.account_key_;
}
int32_t Config::discover_port() {
  return config.discover_port_;
}
int32_t Config::route_port() {
  return config.route_port_;
}
int32_t Config::data_port() {
  return config.data_port_;
}
int Config::refresh_workers_num() {
  return config.refresh_workers_num_;
}
int Config::sync_workers_num() {
  return config.sync_workers_num_;
}
int Config::outer_workers_num() {
  return config.outer_workers_num_;
}
int Config::inner_workers_num() {
  return config.inner_workers_num_;
}
int Config::sync_interval() {
  return config.sync_interval_;
}

void Config::set_discover_port(int32_t discover_port) {
  MutexAuto mutex_auto(&config.mutex);
  config.discover_port_ = discover_port;
}
void Config::set_route_port(int32_t route_port) {
  MutexAuto mutex_auto(&config.mutex);
  config.route_port_ = route_port;
}
void Config::set_data_port(int32_t data_port) {
  MutexAuto mutex_auto(&config.mutex);
  config.data_port_ = data_port;
}
void Config::set_device_name(const string &device_name) {
  MutexAuto mutex_auto(&config.mutex);
  config.device_name_.assign(device_name);
}
void Config::set_device_uuid(const string &device_uuid) {
  MutexAuto mutex_auto(&config.mutex);
  config.device_uuid_.assign(device_uuid);
}
void Config::set_account_name(const string &account_name) {
  MutexAuto mutex_auto(&config.mutex);
  config.account_name_.assign(account_name);
  config.account_key_ = zs::GenAesKey(account_name.c_str());

  string token = config.account_name_ + config.account_passwd_;
  Sha1Hex(token + TokenSha1Salt, &config.token_sha1_);

}
void Config::set_account_passwd(const string &account_passwd) {
  MutexAuto mutex_auto(&config.mutex);
  config.account_passwd_.assign(account_passwd);
  
  string token = config.account_name_ + config.account_passwd_;
  Sha1Hex(token + TokenSha1Salt, &config.token_sha1_);
}
void Config::set_sync_interval(int sync_interval) {
  MutexAuto mutex_auto(&config.mutex);
  config.sync_interval_ = sync_interval;
}
void Config::set_backup_root(const string &backup_root) {
  MutexAuto mutex_auto(&config.mutex);
  config.backup_root_ = backup_root;
}
const string Config::backup_root() {
  MutexAuto mutex_auto(&config.mutex);
  return config.backup_root_;
}
void Config::set_tree_root_prefix(const string &tree_root_prefix) {
  MutexAuto mutex_auto(&config.mutex);
  config.tree_root_prefix_ = tree_root_prefix;
}
const string Config::tree_root_prefix() {
  MutexAuto mutex_auto(&config.mutex);
  return config.tree_root_prefix_;
}

int32_t Config::version() {
  return config.version_;
}

int64_t Config::set_download_cache_volume(int64_t volume) {
  MutexAuto mutex_auto(&config.mutex);
  int64_t old_volume = config.download_cache_volume_;
  config.download_cache_volume_ = volume;
  return old_volume;
}
int64_t Config::download_cache_volume() {
  return config.download_cache_volume_;
}

void Config::set_report_host(const std::string &report_host) {
  MutexAuto mutex_auto(&config.mutex);
  config.report_host_ = report_host;
}

std::string Config::report_host() {
  MutexAuto mutex_auto(&config.mutex);
  return config.report_host_;
}

void Config::set_ca_cert(const std::string &ca_cert) {
  MutexAuto mutex_auto(&config.mutex);
  config.ca_cert_ = ca_cert;
}

std::string Config::ca_cert() {
  MutexAuto mutex_auto(&config.mutex);
  return config.ca_cert_;
}

void Config::set_mac_token(const std::string &mac_token) {
  MutexAuto mutex_auto(&config.mutex);
  config.mac_token_ = mac_token;
}

std::string Config::mac_token() {
  MutexAuto mutex_auto(&config.mutex);
  return config.mac_token_;
}


#ifdef ZS_TEST
void Config::enable_monitor() {
  MutexAuto mutex_auto(&config.mutex);
  config.masks_ &= ~(0x1);
}
void Config::disable_monitor() {
  MutexAuto mutex_auto(&config.mutex);
  config.masks_ |= 0x1;
}
bool Config::is_monitor_enabled() {
  MutexAuto mutex_auto(&config.mutex);
  return (config.masks_ & 0x1) ? false : true;
}
void Config::enable_dht_announce() {
  MutexAuto mutex_auto(&config.mutex);
  config.masks_ &= ~(0x2);
}
void Config::disable_dht_announce() {
  MutexAuto mutex_auto(&config.mutex);
  config.masks_ |= 0x2;
}
bool Config::is_dht_announce_enabled() {
  MutexAuto mutex_auto(&config.mutex);
  return (config.masks_ & 0x2) ? false : true;
}
void Config::enable_broadcast() {
  MutexAuto mutex_auto(&config.mutex);
  config.masks_ &= ~(0x4);
}
void Config::disable_broadcast() {
  MutexAuto mutex_auto(&config.mutex);
  config.masks_ |= 0x4;
}
bool Config::is_broadcast_enabled() {
  MutexAuto mutex_auto(&config.mutex);
  return (config.masks_ & 0x4) ? false : true;
}
void Config::enable_auto_sync() {
  config.masks_ &= ~(0x8);
}
void Config::disable_auto_sync() {
  config.masks_ |= 0x8;
}
bool Config::is_auto_sync_enabled() {
  return (config.masks_ & 0x8) ? false : true;
}
void Config::enable_device_info() {
  config.masks_ &= ~(0x10);
}
void Config::disable_device_info() {
  config.masks_ |= 0x10;
}
bool Config::is_device_info_enabled() {
  return (config.masks_ & 0x10) ? false : true;
}
void Config::enable_push_device_info() {
  config.masks_ &= ~(0x20);
}
void Config::disable_push_device_info() {
  config.masks_ |= 0x20;
}
bool Config::is_push_device_info_enabled() {
  return (config.masks_ & 0x20) ? false : true;
}
void Config::set_test_platform(Platform platform) {
  config.masks_ |= 0x40;
  config.platform_ = platform;
}
Platform Config::test_platform() {
  return config.platform_;
}
bool Config::is_set_test_platform() {
  return (config.masks_ & 0x40) ? true : false;
}
void Config::clear_test_platform() {
  config.masks_ &= ~(0x40);
}

void Config::enable_announce_token_changed() {
  config.masks_ &= ~(0x80);
}
void Config::disable_announce_token_changed() {
  config.masks_ |= 0x80;
}
bool Config::is_announce_token_changed_enabled() {
  return (config.masks_ & 0x80) ? false : true;
}

void Config::disable_refresh() {
  config.masks_ |= 0x100;
}
void Config::enable_refresh() {
  config.masks_ &= ~(0x100);
}
bool Config::is_refresh_enabled() {
  return (config.masks_ & 0x100) ? false : true;
}
  
bool Config::is_default_perms_enabled() {
  return (config.masks_ & 0x200) ? false : true;
}

void Config::enable_default_perms() {
  config.masks_ |= 0x200;
}

void Config::disable_default_perms() {
  config.masks_ &= ~(0x200);
}

#endif

err_t Config::ReadFromContent() {
  IContentResolver *resolver = GetContentResolver();

  const char *projections[] = {
    TableDevice::COLUMN_NAME, TableDevice::COLUMN_UUID,
    TableDevice::COLUMN_DATA_PORT, TableDevice::COLUMN_ROUTE_PORT,
  };
  unique_ptr<ICursor2> cursor(resolver->Query(
          TableDevice::URI, projections, ARRAY_SIZE(projections),
          "%s = %d", TableDevice::COLUMN_ID,
          TableDevice::LOCAL_DEVICE_ID));
  if (!cursor->MoveToNext()) {
    return ZISYNC_ERROR_CONFIG;
  }

  set_device_name(cursor->GetString(0));
  set_device_uuid(cursor->GetString(1));
  set_data_port(cursor->GetInt32(2));
  set_route_port(cursor->GetInt32(3));

  const char *config_pjs[] = {
    TableConfig::COLUMN_NAME, TableConfig::COLUMN_VALUE,
  };
  unique_ptr<ICursor2> config_cursor(resolver->Query(
          TableConfig::URI, config_pjs, ARRAY_SIZE(config_pjs), NULL));
  while (config_cursor->MoveToNext()) {
    const char *name = config_cursor->GetString(0);
    const char *value = config_cursor->GetString(1);
    if (strcmp(name, TableConfig::NAME_USERNAME) == 0) {
      set_account_name(value);
    } else if (strcmp(name, TableConfig::NAME_DISCOVER_PORT) == 0) {
      set_discover_port(atoi(value));
    } else if (strcmp(name, TableConfig::NAME_SYNC_INTERVAL) == 0) {
      set_sync_interval(atoi(value));
    } else if (strcmp(name, TableConfig::NAME_BACKUP_ROOT) == 0) {
      set_backup_root(value);
    } else if (strcmp(name, TableConfig::NAME_TREE_ROOT_PREFIX) == 0) {
      set_tree_root_prefix(value);
    } else if (strcmp(name, TableConfig::NAME_REPORT_HOST) == 0) {
      set_report_host(value);
    } else if (strcmp(name, TableConfig::NAME_CA_CERT) == 0) {
      set_ca_cert(base64_decode(value));
    } else if (strcmp(name, TableConfig::NAME_MAC_TOKEN) == 0) {
      set_mac_token(value);
    }
  }

  return ZISYNC_SUCCESS;
}

Config::Config():download_cache_volume_(DOWNLOAD_CACHE_VOLUME),
    discover_port_(Config::DefaultDiscoverPort), route_port_(Config::DefaultRoutePort),
    data_port_(Config::DefaultDataPort),
    refresh_workers_num_(1), sync_workers_num_(4),
    outer_workers_num_(2), inner_workers_num_(2),
    sync_interval_(3600), version_(version_number) {
#ifdef ZS_TEST
      masks_ = 0;
#endif
    }

const int32_t Config::DefaultDiscoverPort = 8848;
const int32_t Config::DefaultRoutePort = 9527;
const int32_t Config::DefaultDataPort = 9526;

}  // namespace zs
