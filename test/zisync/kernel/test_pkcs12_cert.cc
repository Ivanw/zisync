/**
 * @file test_pkcs12_cert.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Test case for pkcs12_cert.cc.
 *
 * Copyright (C) 2009 Likun Liu <liulikun@gmail.com>
 * Free Software License:
 *
 * All rights are reserved by the author, with the following exceptions:
 * Permission is granted to freely reproduce and distribute this software,
 * possibly in exchange for a fee, provided that this copyright notice appears
 * intact. Permission is also granted to adapt this software to produce
 * derivative works, as long as the modified versions carry this copyright
 * notice and additional notices stating that the work has been modified.
 * This source code may be translated into executable form and incorporated
 * into proprietary software; there is no requirement for such software to
 * contain a copyright notice related to this source.
 *
 * $Id: $
 * $Name: $
 */

#include <UnitTest++/UnitTest++.h>  // NOLINT
#include <openssl/err.h>

#include <cassert>

#include "zisync_kernel.h"  // NOLINT

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/x509_cert.h"
#include "zisync/kernel/utils/pkcs12_cert.h"

using zs::X509Cert;
using zs::DefaultLogger;

static DefaultLogger logger("./Log");

using zs::err_t;
using zs::Pkcs12Cert;

err_t ReadFile(const std::string& path, std::string* data) {
  FILE* fp = fopen(path.c_str(), "rb");
  assert(fp != NULL);
  if (fp == NULL) {
    return zs::ZISYNC_ERROR_OS_IO;
  }

  int ret = fseek(fp, 0, SEEK_END);
  assert(ret != -1);
  if (ret == -1) {
    return zs::ZISYNC_ERROR_OS_IO;
  }

  int offset = static_cast<int>(ftell(fp));

  ret = fseek(fp, 0, SEEK_SET);
  assert(ret != -1);
  
  data->resize(offset);
  ret = fread(const_cast<char*>(data->data()), 1, offset, fp);
  assert(ret == offset);

  return zs::ZISYNC_SUCCESS;
}

TEST(test_Pkcs12Cert) {
  /* Generate the certificate. */
  ZSLOG_INFO("Generating x509 certificate...");
  std::string c("CN");
  std::string st("BJ");
  std::string l("Beijing");
  std::string o("www.zisync.com");
  std::string ou("zisync");
  std::string cn("CA");
  X509Cert x509;
  err_t ret = x509.Create(c, st, l, o, ou, cn);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  ret = x509.SelfSign();
  CHECK(ret == zs::ZISYNC_SUCCESS);

  // x509.PrintToFile(stdout, stdout);
  
  std::string key_pem("pkcs12_x509_key.pem");
  std::string x509_pem("pkcs12_x509_cert.pem");
  ret = x509.PemWriteToFile(key_pem, x509_pem);
  CHECK(ret == zs::ZISYNC_SUCCESS );

  Pkcs12Cert pkcs12;
  ret = pkcs12.Create(x509.pkey(), x509.x509(), NULL);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  std::string pkcs12_file("pkcs12.pfx");
  ret = pkcs12.StoreToFile(pkcs12_file);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  std::string pkcs12_blob;
  ret = pkcs12.StoreToString(&pkcs12_blob);
  CHECK(ret == zs::ZISYNC_SUCCESS);
  
  // reload pkcs12
  Pkcs12Cert pkcs12_2;
  ret = pkcs12.LoadFromFile(pkcs12_file);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  Pkcs12Cert pkcs12_3;
  ret = pkcs12_3.LoadFromString(pkcs12_blob);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  // read file data and cmp
  std::string filedata;
  ret = ReadFile(pkcs12_file, &filedata);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  CHECK(pkcs12_blob == filedata);
}

TEST(test_Pkcs12Cert3) {
  /* Generate the certificate. */
  ZSLOG_INFO("Generating x509 certificate...");
  std::string c("CN");
  std::string st("BJ");
  std::string l("Beijing");
  std::string o("www.zisync.com");
  std::string ou("zisync");
  std::string cn("CA");
  X509Cert x509_ca;
  err_t ret = x509_ca.Create(c, st, l, o, ou, cn);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  ret = x509_ca.SelfSign();
  CHECK(ret == zs::ZISYNC_SUCCESS);

  // x509.PrintToFile(stdout, stdout);
  
  std::string key_pem("pkcs12_x509_ca_key.pem");
  std::string x509_pem("pkcs12_x509_ca_cert.pem");
  ret = x509_ca.PemWriteToFile(key_pem, x509_pem);
  CHECK(ret == zs::ZISYNC_SUCCESS );

  Pkcs12Cert pkcs12;
  ret = pkcs12.Create(x509_ca.pkey(), x509_ca.x509(), NULL);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  cn = "server";
  X509Cert x509_server;
  ret = x509_server.Create(c, st, l, o, ou, cn);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  ret = x509_server.SelfSign();
  CHECK(ret == zs::ZISYNC_SUCCESS);
  
  std::string chain_blob;
  std::string server_blob;
  ret = x509_server.PemAppendX509ToString(&server_blob);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  ret = pkcs12.Verify(server_blob, chain_blob);
  CHECK(ret != zs::ZISYNC_SUCCESS);

  ret = x509_server.SignWith(x509_ca.pkey(), x509_ca.x509(), true);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  server_blob.clear();
  ret = x509_server.PemAppendX509ToString(&server_blob);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  ret = pkcs12.Verify(server_blob, chain_blob);
  CHECK(ret == zs::ZISYNC_SUCCESS);
}

TEST(test_Pkcs12Cert2) {
  /* Generate the certificate. */
  ZSLOG_INFO("Generating x509 certificate...");
  std::string c("CN");
  std::string st("BJ");
  std::string l("Beijing");
  std::string o("www.zisync.com");
  std::string ou("zisync");
  std::string cn("CA");
  X509Cert x509_ca;
  err_t ret = x509_ca.Create(c, st, l, o, ou, cn);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  ret = x509_ca.SelfSign();
  CHECK(ret == zs::ZISYNC_SUCCESS);

  // x509.PrintToFile(stdout, stdout);
  
  std::string key_pem("pkcs12_x509_ca_key.pem");
  std::string x509_pem("pkcs12_x509_ca_cert.pem");
  ret = x509_ca.PemWriteToFile(key_pem, x509_pem);
  CHECK(ret == zs::ZISYNC_SUCCESS );

  Pkcs12Cert pkcs12;
  ret = pkcs12.Create(x509_ca.pkey(), x509_ca.x509(), NULL);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  cn = "server";
  X509Cert x509_server;
  ret = x509_server.Create(c, st, l, o, ou, cn);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  ret = x509_server.SignWith(x509_ca.pkey(), x509_ca.x509(), true);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  std::string chain_blob;
  ret = x509_server.PemAppendX509ToString(&chain_blob);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  cn = "user";
  X509Cert x509_user;
  ret = x509_user.Create(c, st, l, o, ou, cn);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  ret = x509_user.SignWith(x509_server.pkey(),
                           x509_server.x509(), true);
  CHECK(ret == zs::ZISYNC_SUCCESS);
  
  std::string user_blob;
  ret = x509_user.PemAppendX509ToString(&user_blob);
  
  ret = pkcs12.Verify(user_blob, std::string());  // with no chain, should fail
  CHECK(ret != zs::ZISYNC_SUCCESS);

  ret = pkcs12.Verify(user_blob, chain_blob); 
  CHECK(ret == zs::ZISYNC_SUCCESS);

  cn = "subuser";
  X509Cert x509_subuser;
  ret = x509_subuser.Create(c, st, l, o, ou, cn);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  ret = x509_subuser.SignWith(x509_user.pkey(),
                           x509_user.x509(), false);
  CHECK(ret == zs::ZISYNC_SUCCESS);
  
  std::string subuser_blob;
  ret = x509_subuser.PemAppendX509ToString(&subuser_blob);
  
  ret = pkcs12.Verify(subuser_blob, chain_blob);  // with broken chain, should fail
  CHECK(ret != zs::ZISYNC_SUCCESS);

  x509_user.PemAppendX509ToString(&chain_blob);
  ret = pkcs12.Verify(user_blob, chain_blob); 
  CHECK(ret == zs::ZISYNC_SUCCESS);

  cn = "subsubuser";
  X509Cert x509_subsubuser;
  ret = x509_subsubuser.Create(c, st, l, o, ou, cn);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  ret = x509_subsubuser.SignWith(x509_subuser.pkey(),
                           x509_subuser.x509(), false);
  CHECK(ret == zs::ZISYNC_SUCCESS);
}



//#ifndef _MSC_VER

int main(int argc , char** argv) {
  SSLeay_add_all_algorithms();
  ERR_load_crypto_strings();

  logger.Initialize();
  logger.error_to_stderr = true;
  zs::LogInitialize(&logger);
  UnitTest::RunAllTests();
  zs::LogCleanUp();
  logger.CleanUp();
  return 0;
}
//#endif

