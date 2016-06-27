/**
 * @file test_icore.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Test cases for icore.cc.
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

#include <stdarg.h>
#include <string.h>

#include <UnitTest++/UnitTest++.h>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"

#ifdef _MSC_VER
/*MSVC*/
#endif
#ifdef __GNUC__
/*GCC*/
using zs::err_t;
#endif

using zs::StringFormat;
using zs::StringFormatV;
using zs::StringAppendFormat;
// using zs::StringAppendFormatV;
using zs::StringImplode;
using zs::StringStartsWith;
using zs::HexToBin;
using zs::BinToHex;
using zs::ConvertBase16To32;
using zs::ConvertBase32To16;
using zs::VerifyKey;

TEST(test_CaseName) {
  int ret = 0;
  CHECK_EQUAL(0, ret);
}

TEST(test_StringFormat_1) {
  int32_t i32 = 123;
  int64_t i64 = 0xFFFFFFFFFFFF;

  std::string result;
  CHECK(StringFormat(&result, "%d %" PRId64, i32, i64) > 0);
  CHECK_EQUAL("123 281474976710655", result);

  CHECK(StringFormat(&result, "%s%s", "a", "bc") > 0);
  CHECK_EQUAL("abc", result);

  CHECK_EQUAL(0, StringFormat(&result, "%s", ""));
  CHECK_EQUAL("", result);

  CHECK(StringFormat(&result, "%s %s", "very long string", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") > 0);  // NOLINT

  CHECK_EQUAL("very long string aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", result);  // NOLINT
}

TEST(test_StringAppendFormat_1) {
  int32_t i32 = 123;
  int64_t i64 = 0xFFFFFFFFFFFF;

  std::string result("abcdefg");
  CHECK(StringAppendFormat(&result, "%d %" PRId64, i32, i64) > 0);
  CHECK_EQUAL("abcdefg123 281474976710655", result);

  result = "abcdefg";
  CHECK(StringAppendFormat(&result, "%s%s", "a", "bc") > 0);
  CHECK_EQUAL("abcdefgabc", result);

  result = "ab";
  CHECK_EQUAL(0, StringAppendFormat(&result, "%s", ""));
  CHECK_EQUAL("ab", result);

  result = "hello";
  CHECK(StringAppendFormat(&result, " %s %s", "very long string", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") > 0);  // NOLINT

  CHECK_EQUAL("hello very long string aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", result);  // NOLINT
}

int StringFormatVWrapper(std::string* result, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int rc = StringFormatV(result, fmt, ap);
  va_end(ap);

  return rc;
}

TEST(test_StringFormatV_1) {
  int32_t i32 = 123;
  int64_t i64 = 0xFFFFFFFFFFFF;

  std::string result;
  CHECK(StringFormatVWrapper(&result, "%d %" PRId64, i32, i64) > 0);
  CHECK_EQUAL("123 281474976710655", result);

  CHECK(StringFormatVWrapper(&result, "%s%s", "a", "bc") > 0);
  CHECK_EQUAL("abc", result);

  CHECK_EQUAL(0, StringFormatVWrapper(&result, "%s", ""));
  CHECK_EQUAL("", result);

  CHECK(StringFormatVWrapper(&result, "%s %s", "very long string", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") > 0);  // NOLINT

  CHECK_EQUAL("very long string aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", result);  // NOLINT
}


TEST(test_StringImplode) {
  std::string result;
  const char* argv1[] = {
    "123"
  };

  CHECK_EQUAL(zs::ZISYNC_SUCCESS, StringImplode(&result, argv1, 1, ", "));
  CHECK_EQUAL("123", result);

  const char* argv3[] = {
    "123", "456", "789"
  };


  result = "";
  CHECK(StringImplode(&result, argv3, 3, ", ") == zs::
        ZISYNC_SUCCESS);
  CHECK_EQUAL("123, 456, 789", result);
  // printf("%s\n", result.c_str());
}

TEST(test_StringStartsWith1) {
  CHECK(StringStartsWith("", ""));
  CHECK_EQUAL(true, StringStartsWith("a", ""));
  CHECK_EQUAL(false, StringStartsWith("", "b"));
  CHECK(StringStartsWith("a", "a"));
  CHECK_EQUAL(false, StringStartsWith("a", "b"));
  CHECK(StringStartsWith("ab", "a"));
}

TEST(test_StringStartsWith2) {
  CHECK(StringStartsWith(std::string(""), std::string("")));
  CHECK_EQUAL(true, StringStartsWith(std::string("a"), std::string("")));
  CHECK_EQUAL(false, StringStartsWith(std::string(""), std::string("b")));
  CHECK(StringStartsWith(std::string("a"), std::string("a")));
  CHECK_EQUAL(false, StringStartsWith(std::string("a"), std::string("b")));
  CHECK(StringStartsWith(std::string("ab"), std::string("a")));
}

TEST(test_BinToHex) {
  std::string hex_ret;

  BinToHex('X', std::string(""), &hex_ret);
  CHECK(hex_ret.compare("") == 0);
  BinToHex('x', std::string(""), &hex_ret);
  CHECK(hex_ret.compare("") == 0);

  std::string bin("\x98\x45\xb6\xc7\x13\xa2\x5f\x97\x40\x8e\xed\x36\x52\x3b\xb5\x87\x45\x97\x46\xa6");
  std::string hex("9845b6c713a25f97408eed36523bb587459746a6");
  BinToHex('x', bin, &hex_ret);
  CHECK(hex_ret == hex);
                  
  const char* bin_sz = "\x98\x45\xb6\xc7\x13\xa2\x5f\x97\x40\x8e\xed\x36\x52\x3b\xb5\x87\x45\x97\x46\xa6";
  BinToHex('x', bin_sz, 20,  &hex_ret);
  CHECK(hex_ret == hex);
  
  std::string bin1("\x98\x45\xb6\xC7\x13\xA2\x5F\x97\x40\x8E\xED\x36\x52\x3b\xB5\x87\x45\x97\x46\xA6");
  std::string hex1("9845B6C713A25F97408EED36523BB587459746A6");
  BinToHex('X', bin1, &hex_ret);
  CHECK(hex_ret == hex1);

  const char* bin_sz1 = "\x98\x45\xb6\xC7\x13\xA2\x5F\x97\x40\x8E\xED\x36\x52\x3b\xB5\x87\x45\x97\x46\xA6";
  BinToHex('X', bin_sz1, 20, &hex_ret);
  CHECK(hex_ret == hex1);
}

TEST(test_HexToBin) {
  std::string bin;
  HexToBin(std::string(""), &bin);
  CHECK(bin.compare("") == 0);
  // HexToBin(std::string("--wf-"), &bin);
  // CHECK(bin.compare("") == 0);
  HexToBin(std::string("2"), &bin);
  CHECK(bin.compare("") == 0);
  HexToBin(std::string("e"), &bin);
  CHECK(bin.compare("") == 0);
  HexToBin("00", &bin);
  CHECK(bin.compare(std::string(1, '\x0')) == 0);
  HexToBin("0a", &bin);
  CHECK(bin.compare(std::string(1, '\x0A')) == 0);
  HexToBin("0A", &bin);
  CHECK(bin.compare(std::string(1, '\x0A')) == 0);
  HexToBin("af", &bin);
  CHECK(bin.compare(std::string(1, '\xAF')) == 0);
  HexToBin("AF", &bin);
  CHECK(bin.compare(std::string(1, '\xAF')) == 0);
  HexToBin("aa", &bin);
  CHECK(bin.compare(std::string(1, '\xAA')) == 0);
  HexToBin("AA", &bin);
  CHECK(bin.compare(std::string(1, '\xAA')) == 0);
  HexToBin("ff", &bin);
  CHECK(bin.compare(std::string(1, '\xFF')) == 0);
  HexToBin("FF", &bin);
  CHECK(bin.compare(std::string(1, '\xFF')) == 0);
  HexToBin("123", &bin);
  CHECK(bin.compare(std::string(1, '\x12')) == 0);
  HexToBin("22a", &bin);
  CHECK(bin.compare(std::string(1, '\x22')) == 0);
  HexToBin("22a", &bin);
  CHECK(bin.compare(std::string(1, '\x22')) == 0);
  char ret0[5] = {'\x12', '\x3d', '\x34', '\xa7', '\x6c'};
  HexToBin("123d34a76c", &bin);
  CHECK(bin.compare(std::string(ret0, 5)) == 0);
  HexToBin("123d34a76c", &bin);
  CHECK(bin.compare(std::string(ret0, 5)) == 0);
  HexToBin("123d34a76cc", &bin);
  CHECK(bin.compare(std::string(ret0, 5)) == 0);
  HexToBin("123d34a76cc", &bin);
  CHECK(bin.compare(std::string(ret0, 5)) == 0);
  char ret1[20] = {
    '\x98', '\x45', '\xb6', '\xc7', '\x13', '\xa2', '\x5f', '\x97', '\x40', '\x8e',
    '\xed', '\x36', '\x52', '\x3b', '\xb5', '\x87', '\x45', '\x97', '\x46', '\xa6'
  };
  std::string hex;
  hex.assign("9845b6c713a25f97408eed36523bb587459746a6", 40);
  HexToBin(hex, &bin);
  CHECK(bin.compare(std::string(ret1, 20)) == 0);
}

TEST(test_ConvertBase16To32) {
  std::string base32 = "ABCDEFGHIJKLMNOPQRSTUV0123456789";
  std::string base16 = "0123456789abcdef";
  std::string number; 
  std::string out;

  number = "77f4b354af1d139b0ffa6dc652eebfea48c94";
  //out = "D18S3VJL27CONQ98TN23JO7P9KJDEU";
  out = ConvertBase16To32(number, base16, base32);
  CHECK(strcmp(out.c_str(), "D18S3VJL27CONQ98TN23JO7P9KJDEU") == 0);

  number = "6d1acad138f80a50215de3e4d20bc10254694";
  //out = "DNDLFNCOH2BJICC1PD6TJA1QICKRUU";
  out = ConvertBase16To32(number, base16, base32);
  CHECK(strcmp(out.c_str(), "DNDLFNCOH2BJICC1PD6TJA1QICKRUU") == 0);

  /* change base */
  base32 = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
  base16 = "0123456789abcdef";

  number = "e7711ac0d93b56a0b87829739f2825e398b70";
  //out = "77E4DC1M9RAQGBGU19EEFIG9F3J2RG";
  out = ConvertBase16To32(number, base16, base32);
  CHECK(strcmp(out.c_str(), "77E4DC1M9RAQGBGU19EEFIG9F3J2RG") == 0);

  number = "dc4360b062624f12409475a06dd009a1644c1";
  //out = "6S8DGB0OJ29S94153LK1MT02D1CH61";
  out = ConvertBase16To32(number, base16, base32);
  CHECK(strcmp(out.c_str(), "6S8DGB0OJ29S94153LK1MT02D1CH61") == 0);
}

TEST(test_ConvertBase32To16) {
  std::string base32 = "ABCDEFGHIJKLMNOPQRSTUV0123456789";
  std::string base16 = "0123456789abcdef";
  std::string number; 
  std::string out_c, out;

  number = "D18S3VJL27CONQ98TN23JO7P9KJDEU";
  out_c = "77f4b354af1d139b0ffa6dc652eebfea48c94";
  out = ConvertBase32To16(number, base32, base16);
  CHECK(strcmp(out.c_str(), out_c.c_str()) == 0);

  number = "DNDLFNCOH2BJICC1PD6TJA1QICKRUU";
  out_c = "6d1acad138f80a50215de3e4d20bc10254694";
  out = ConvertBase32To16(number, base32, base16);
  CHECK(strcmp(out.c_str(), out_c.c_str()) == 0);

  /* change base */
  base32 = "0123456789ABCDEFGHIJKLMNOPQRSTUV";
  base16 = "0123456789abcdef";

  number = "77E4DC1M9RAQGBGU19EEFIG9F3J2RG";
  out_c = "e7711ac0d93b56a0b87829739f2825e398b70";
  out = ConvertBase32To16(number, base32, base16);
  CHECK(strcmp(out.c_str(), out_c.c_str()) == 0);

  number = "6S8DGB0OJ29S94153LK1MT02D1CH61";
  out_c = "dc4360b062624f12409475a06dd009a1644c1";
  out = ConvertBase32To16(number, base32, base16);
  CHECK(strcmp(out.c_str(), out_c.c_str()) == 0);
}

TEST(test_VerifyKey) {
  std::string key = "D18S3-VJL27-CONQ9-8TN23-JO7P9-KJDEU";
  CHECK(VerifyKey(key));

  key = "DNDLF-NCOH2-BJICC-1PD6T-JA1QI-CKRUU";
  CHECK(VerifyKey(key));

  key = "11111-D18S3-VJL27-CONQ9-8TN23-JO7P9-KJDEU";
  CHECK(!VerifyKey(key));

  key = "D18S3-VJL27-CONQ9-8TN23-JO7P9-KJDEU-1111";
  CHECK(!VerifyKey(key));

  key = "";
  CHECK(!VerifyKey(key));

  key = "12ejwelfj;wsf9w8fs;odjf;sojif";
  CHECK(!VerifyKey(key));

  key = "D18S3-VJL27-CONQ9-8TN23-JY7W9-KZDEZ";
  CHECK(!VerifyKey(key));
}
