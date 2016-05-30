/**
 * @file test_x509_cert.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief test case for x509_cert.
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

#include "zisync_kernel.h"  // NOLINT

#include "zisync/kernel/zslog.h"
#include "zisync/kernel/utils/x509_cert.h"

using zs::err_t;
using zs::X509Cert;
using zs::DefaultLogger;

static DefaultLogger logger("./Log");

TEST(test_X509Cert) {
  /* Generate the certificate. */
  ZSLOG_INFO("Generating x509 certificate...");
  std::string c("CN");
  std::string st("BJ");
  std::string l("Beijing");
  std::string o("www.zisync.com");
  std::string ou("zisync");  // sync_uuid
  std::string cn("CA");      // tree_uuid
  X509Cert x509;
  err_t ret = x509.Create(c, st, l, o, ou, cn);
  CHECK(ret == zs::ZISYNC_SUCCESS);

  ret = x509.SelfSign();
  CHECK(ret == zs::ZISYNC_SUCCESS);
  
  // x509.PrintToFile(stdout, stdout);
  
  /* Write the private key and certificate out to disk. */
  std::string key_pem("x509_key.pem");
  std::string x509_pem("x509_cert.pem");
  ret = x509.PemWriteToFile(key_pem, x509_pem);
    
  CHECK(ret == zs::ZISYNC_SUCCESS);
}

// #ifndef _MSC_VER

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
// #endif
