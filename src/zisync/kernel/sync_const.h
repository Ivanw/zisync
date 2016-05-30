// Copyright 2014, zisync.com
#ifndef ZISYNC_KERNEL_SYNC_CONST_H_
#define ZISYNC_KERNEL_SYNC_CONST_H_

#include <string>
#include <cstdint>

namespace zs {

#ifdef NDEBUG
//const char kReportHost[] = "verify.plusync.com";
//const char kReportHost[] = "stat.zisync.com";
const char kReportHost[] = "stat2.zisync.com";
#else
//const char kReportHost[] = "verify.debug.plusync.com";
//const char kReportHost[] = "10.88.1.88";
//const char kReportHost[] = "10.88.1.160";
//const char kReportHost[] = "stat.debug.zisync.com";
const char kReportHost[] = "stat2.debug.zisync.com";
#endif

const char kTrackerUri[] = "http://tracker.zisync.com:80";

const char kUiEventHost[] = "api.ui.zisync.com";
const char kReportUiEventUri[] = "tcp://api.ui.zisync.com:80";

const char SYNC_FILE_TASKS_META_FILE[] = ".zisync.meta";
const char PULL_DATA_TEMP_DIR[] = ".zstm";

const int32_t META_FILE_NUM_LIMIT = 10000;
#if defined(__ANDROID__) || defined(macintosh) || defined(__APPLE__) \
|| defined(__APPLE_CC__)
const int32_t TRANSFER_FILE_SIZE_LIMIT = 1024 * 1024 * 4;
#else
const int32_t TRANSFER_FILE_SIZE_LIMIT = 1024 * 1024 * 128;
#endif
const int32_t APPLY_BATCH_NUM_LIMIT = 500;
const int32_t FIND_LIMIT = 10000, WAIT_RESPONSE_TIMEOUT_IN_S = 5;
const int64_t DEVICE_NO_RESP_OFFLINE_TIMEOUT_IN_S = 360; 

const int64_t DEVICE_NO_RESP_OFFLINE_CHECK_INTERVAL_IN_MS = 120000;
const int64_t DEVICE_INFO_INTERVAL_IN_MS = 120000;
const int64_t REFRESH_INTERVAL_IN_MS = 1800000;
const int64_t SYNC_INTERVAL_IN_MS = 600000;
const int64_t MOBILE_DEVICE_SYNC_INTERVAL_IN_MS = 600000;
const int64_t REPORT_INTERVAL_IN_MS = 3600000;
const int64_t KREPORT_UI_MONITER_INTERVAL_IN_MS = 3600000;

const int64_t BACKGROUND_INTERVAL_IN_MS = 60000;
const int64_t BACKGROUND_ALIVE_TIME_IN_S = 300;

const int32_t DISCOVER_DEVICE_TIMEOUT_IN_S = 5;

const std::string zisync_version = "2.1.1";
const int32_t version_number = 27;

const int32_t NULL_TREE_ID = -1;
const int64_t DOWNLOAD_CACHE_VOLUME = 200 * 1024 * 1024;
const char CAname[] = "ca.crt";
#ifndef NDEBUG
const char cert_host[] = "client";
const char CAcert[] =
//    "-----BEGIN CERTIFICATE-----"
    "MIIC3DCCAkWgAwIBAgIJAN0Z42iTTpDCMA0GCSqGSIb3DQEBBQUAMIGGMQswCQYD"
    "VQQGEwJDTjEOMAwGA1UECAwFY2hpbmExEDAOBgNVBAcMB0JlaWppbmcxEDAOBgNV"
    "BAoMB1BsdXN5bmMxEDAOBgNVBAsMB1BsdXN5bmMxEjAQBgNVBAMMCWxvY2FsaG9z"
    "dDEdMBsGCSqGSIb3DQEJARYOd3d3Lnppc3luYy5jb20wHhcNMTUwNDI3MTE0NTU2"
    "WhcNMTYwNDI2MTE0NTU2WjCBhjELMAkGA1UEBhMCQ04xDjAMBgNVBAgMBWNoaW5h"
    "MRAwDgYDVQQHDAdCZWlqaW5nMRAwDgYDVQQKDAdQbHVzeW5jMRAwDgYDVQQLDAdQ"
    "bHVzeW5jMRIwEAYDVQQDDAlsb2NhbGhvc3QxHTAbBgkqhkiG9w0BCQEWDnd3dy56"
    "aXN5bmMuY29tMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDJHMxpIUVsOqd7"
    "7GRNU8pAfwsdcHvzG3lbuJvUe338olnxDdMoa3TGPgSLivmClw3zjOWkmgQ4mXfb"
    "SBGvm//QJb/gqWDPzVCOoGPPJFhYQpFBWlvJn7ODndxX43WhxevoKtuTBVGxtxfb"
    "qEZegr1j90xe3n32rTbL5sBvLouAYwIDAQABo1AwTjAdBgNVHQ4EFgQUH5edc3dg"
    "wqgk7rIPY9oL2/BBbtkwHwYDVR0jBBgwFoAUH5edc3dgwqgk7rIPY9oL2/BBbtkw"
    "DAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQUFAAOBgQCMz2jnB+1H+t8PbEm3I4uF"
    "ACRyuc8Pa1wb61cMJmX1yL1BQfReMDSXXoSeQrwCTKjDaiHK5OBP4XhKmxscnky7"
    "2yvbpaS0hv3u9QABi//rkDTnrpvrep5BiBPBPjYyUXMGiVehovJw5rWlDYlvYbJd"
    "ok1UjCg9K+3Hp+7CwYkgLQ==";
 //   "-----END CERTIFICATE-----";
#else
const char cert_host[] = "plusync.com";
const char CAcert[] =
    "MIIEATCCAumgAwIBAgIJAPGrnp/jUysAMA0GCSqGSIb3DQEBCwUAMIGWMQswCQYD"
    "VQQGEwJDSDEQMA4GA1UECAwHQmVpamluZzEQMA4GA1UEBwwHQmVpamluZzEQMA4G"
    "A1UECgwHUGx1c3luYzEQMA4GA1UECwwHUGx1c3luYzEbMBkGA1UEAwwSY2xpZW50"
    "LnBsdXN5bmMuY29tMSIwIAYJKoZIhvcNAQkBFhNzdXBwb3J0QHBsdXN5bmMuY29t"
    "MB4XDTE1MDUwNTA2NTExMVoXDTI1MDUwMjA2NTExMVowgZYxCzAJBgNVBAYTAkNI"
    "MRAwDgYDVQQIDAdCZWlqaW5nMRAwDgYDVQQHDAdCZWlqaW5nMRAwDgYDVQQKDAdQ"
    "bHVzeW5jMRAwDgYDVQQLDAdQbHVzeW5jMRswGQYDVQQDDBJjbGllbnQucGx1c3lu"
    "Yy5jb20xIjAgBgkqhkiG9w0BCQEWE3N1cHBvcnRAcGx1c3luYy5jb20wggEiMA0G"
    "CSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCxKZ2C84okoUG1xG53C74/Gb7DjyNT"
    "MZbaFJhotSpDznFojWaKG7gADgTELXOVBAfI9GdAJBv1FFqJNbt+zEQtJxqxBE/S"
    "GG5pe8850ZgoxNMr8oZTdNuNtfC4DYw6XpYTwJMN4iAfd2bXDcESDERSs0lFG+U3"
    "8aPb0C7gwBCjoRs8p/pUO0vuAcMGD/DrptAtd9qvVtJonOrAdlC7JzF2TlpRcQwp"
    "tQNJNDp3IfkQJMWna/IfLTXZgluyk0UsIAfuAiPZU2yMHA0+Qw4hXrVo5wjfZnqQ"
    "gvKUXcZxDdoKVJJ4oojlENL0G4pjQEqlYXcBA6gQ25um31hx1dClBZvjAgMBAAGj"
    "UDBOMB0GA1UdDgQWBBSbP49x0xvIx2dp5v5eb1N70hObGjAfBgNVHSMEGDAWgBSb"
    "P49x0xvIx2dp5v5eb1N70hObGjAMBgNVHRMEBTADAQH/MA0GCSqGSIb3DQEBCwUA"
    "A4IBAQAfYMOKelYDu3e0TMJ/AzZEUwUkHdSlHMPtX1QiGLHtXwtM9LHns13FBHKB"
    "4VUI1E1X9INNo2G52AC97rd5MEQzSnRnA4w20twXVr37+Q/MRRIk4CYEP9vYbdsB"
    "oCMesej3R8AKIyNMNteM8ErwKu0Q1jeOLkjwFMHjzbyuloxQN90M5N7px+sRGS+U"
    "w4yuY6+4hVxZiMkJLH/07K9r28dmOqawXii+9PwSuwbkTqZ2VsOk/2QxI1XJmC3Y"
    "PbH2P0ifFy/NuuUjnKLylLUBSKFFdpHPhcJ0+8RFZr4O1weyQOdUla+4pwpz1JXh"
    "qO5dTQ+MsLn8m3tDG+PUaUFkjnJB";
#endif
const int32_t HTTP_CONNECT_TIMEOUT = 15;  // seconds

const int64_t AUTO_BIND_INTERVAL_S = 900;
  
}  // namespace zs

#endif  // ZISYNC_KERNEL_SYNC_CONST_H_
