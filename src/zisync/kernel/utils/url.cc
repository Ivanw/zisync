#include <assert.h>

#include "zisync/kernel/utils/url.h"

namespace zs {

static inline unsigned char ToHex(unsigned char x) {
  return x > 9 ? x + 55 : x + 48;
}

static inline char FromHex(unsigned char x) {
  unsigned char y;
  if (x >= 'A' && x <= 'Z')
    y = x - 'A' + 10;
  else if (x >= 'a' && x <= 'z')
    y = x - 'a' + 10;
  else if (x >= '0' && x <= '9')
    y = x - '0';
  else
    assert(0);

  return y;
}

std::string UrlEncode(const std::string &str) {
  std::string strTemp;
  size_t length = str.length();
  for (size_t i = 0; i < length; i++) {
    if (isalnum((unsigned char)str[i]) ||
        (str[i] == '-') ||
        (str[i] == '_') ||
        (str[i] == '~')) {
      strTemp += str[i];
    } else if (str[i] == ' ') {
      strTemp += "+";
    } else {
      strTemp += '%';
      strTemp += ToHex((unsigned char)str[i] >> 4);
      strTemp += ToHex((unsigned char)str[i] % 16);
    }
  }

  return strTemp;
}

std::string UrlDecode(const std::string &str) {
  std::string strTemp;
  size_t length = str.length();
  for (size_t i = 0; i < length; i++) {
    if (str[i] == '+') {
      strTemp += ' ';
    } else if (str[i] == '%') {
      assert(i + 2 < length);
      unsigned char high = FromHex((unsigned char)str[++i]);
      unsigned char low = FromHex((unsigned char)str[++i]);
      strTemp += high * 16 + low;
    } else {
      strTemp += str[i];
    }
  }

  return strTemp;
}

void GenFixedStringForHttpUri(std::string *uri) {
  if (uri == NULL) {
    return;
  }
  size_t pos = uri->find_last_not_of('/');
  if (pos != uri->size() - 1) {
    uri->erase(pos + 1, uri->size() - pos - 1);
  }
}

}  // namespace zs
