/* Copyright [2014] <zisync.com> */

#include <stdarg.h>  // for va_start, etc
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <regex>

#include <memory>    // for std::unique_ptr

#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/utils/sha.h"

namespace zs {

// const std::string StringFormat(const char* format, ...) {
//   string ret_string;
//   va_list ap;
//   va_start(ap, format);
//   StringFormatV(&ret_string, format, ap);
//   return ret_string;
// }

int StringFormat(std::string* result, const char* format, ...) {
  /* reserve 2 times as much as the length of the format */
  int final_n;
  size_t n = 100 * 2;
  va_list ap;

  while (1) {
    result->resize(n);
    char* buffer = &(*result->begin());

    va_start(ap, format);
#if defined _WIN32 || defined _WIN64
    final_n = vsnprintf_s(buffer, _TRUNCATE, n, format, ap);
#else
    final_n = vsnprintf(buffer, n, format, ap);
#endif
    va_end(ap);

    if (final_n < 0 || final_n >= static_cast<int>(n)) {
       n += ::abs(static_cast<int>(final_n - n + 1));
    } else {
      result->resize(final_n);
      break;
    }
  }

  return final_n;
}

/*
 * format using std::string rather than const std::string&, because
 * va_start has undefined behavior with reference types on mac platform
 */
int StringFormat(std::string* result, std::string format, ...) {
  /* reserve 2 times as much as the length of the format */
  int final_n;
  size_t n = format.size() * 2;
  va_list ap;

  while (1) {
    result->resize(n);
    char* buffer = &(*result->begin());

    va_start(ap, format);
#if defined _WIN32 || defined _WIN64
    final_n = vsnprintf_s(buffer, _TRUNCATE, n, format.c_str(), ap);
#else
    final_n = vsnprintf(buffer, n, format.c_str(), ap);
#endif
    va_end(ap);

    if (final_n < 0 || final_n >= static_cast<int>(n)) {
      n += ::abs(static_cast<int>(final_n - n + 1));
    } else {
      result->resize(final_n);
      break;
    }
  }

  return final_n;
}


int StringFormatV(std::string* result, const char* format, va_list ap) {
  /* reserve 2 times as much as the length of the format */
  int final_n;
  size_t n = 100 * 2;
  va_list aq;

  while (1) {
    result->resize(n);
    char* buffer = &(*result->begin());

    va_copy(aq, ap);

#if defined _WIN32 || defined _WIN64
    final_n = vsnprintf_s(buffer, _TRUNCATE, n, format, aq);
#else
    final_n = vsnprintf(buffer, n, format, aq);
#endif

    va_end(aq);

    if (final_n < 0 || final_n >= static_cast<int>(n)) {
      n += ::abs(static_cast<int>(final_n - n + 1));
    } else {
      result->resize(final_n);
      break;
    }
  }

  return final_n;
}

int StringFormatV(std::string* result, std::string format, va_list ap) {
  /* reserve 2 times as much as the length of the format */
  int final_n;
  size_t n = format.size() * 2;
  va_list aq;

  while (1) {
    result->resize(n);
    char* buffer = &(*result->begin());

    va_copy(aq, ap);

#if defined _WIN32 || defined _WIN64
    final_n = vsnprintf_s(buffer, _TRUNCATE, n, format.c_str(), aq);
#else
    final_n = vsnprintf(buffer, n, format.c_str(), aq);
#endif
    va_end(aq);

    if (final_n < 0 || final_n >= static_cast<int>(n)) {
      n += ::abs(static_cast<int>(final_n - n + 1));
    } else {
      result->resize(final_n);
      break;
    }
  }

  return final_n;
}


int StringAppendFormat(std::string* result, const char* format, ...) {
  /* reserve 2 times as much as the length of the format */
  int final_n;
  size_t n = 100 * 2, offset = result->size();
  va_list ap;

  while (1) {
    result->resize(offset + n);
    char* buffer = &(*result->begin()) + offset;

    va_start(ap, format);

#if defined _WIN32 || defined _WIN64
    final_n = vsnprintf_s(buffer, _TRUNCATE, n, format, ap);
#else
    final_n = vsnprintf(buffer, n, format, ap);
#endif

    va_end(ap);

    if (final_n < 0 || final_n >= static_cast<int>(n)) {
      n += ::abs(static_cast<int32_t>(final_n - n + 1));
    } else {
      result->resize(offset + final_n);
      break;
    }
  }

  return final_n;
}

int StringAppendFormat(std::string* result, std::string format, ...) {
  /* reserve 2 times as much as the length of the format */
  int final_n;
  size_t n = format.size() * 2, offset = result->size();
  va_list ap;

  while (1) {
    result->resize(offset + n);
    char* buffer = &(*result->begin()) + offset;

    va_start(ap, format);

#if defined _WIN32 || defined _WIN64
    final_n = vsnprintf_s(buffer, _TRUNCATE, n, format.c_str(), ap);
#else
    final_n = vsnprintf(buffer, n, format.c_str(), ap);
#endif

    va_end(ap);

    if (final_n < 0 || final_n >= static_cast<int>(n)) {
      n += ::abs(static_cast<int32_t>(final_n - n + 1));
    } else {
      result->resize(offset + final_n);
      break;
    }
  }

  return final_n;
}

err_t StringImplode(
    std::string* buffer, const char* argv[], int argc, const char* glue) {
  assert(buffer != NULL && argv != NULL);

  for (int i = 0; i < argc; i++) {
    if (i > 0) {
      buffer->append(glue);
    }
    buffer->append(argv[i]);
  }

  return ZISYNC_SUCCESS;
}

bool StringStartsWith(const char* str, const char* prefix) {
  if (!str || !prefix) {
    if (!str && !prefix)
      return true;
    else
      return false;
  }

  while (*prefix && *str) {
    if (*prefix++ != *str ++)
      return false;
  }

  if (*prefix == '\0') {
    return true;
  } else {
    return false;
  }
}

bool StringStartsWith(const std::string& str, const std::string& prefix) {
  // if (str.empty() || prefix.empty()) {
  //   if (str.empty() && prefix.empty())
  //     return true;
  //   else
  //     return false;
  // }
  if (str.empty() && prefix.empty()) {
    return true;
  }

  const char* prefix1 = prefix.c_str();
  const char* str1 = str.c_str();
  while (*prefix1 && *str1) {
    if (*prefix1++ != *str1++)
      return false;
  }

  if (*prefix1 == '\0') {
    return true;
  } else {
    return false;
  }
}

inline void  CharToHex(char fmt, char byte, char hex[2])  {
  static const char xtab_lower[] = "0123456789abcdef";
  static const char xtab_upper[] = "0123456789ABCDEF";
  if (fmt == 'x') {
    hex[0] = xtab_lower[(byte >> 4) & 0xF];
    hex[1] = xtab_lower[byte & 0xF];
  } else {
    assert(fmt == 'X');
    hex[0] = xtab_upper[(byte >> 4) & 0xF];
    hex[1] = xtab_upper[byte & 0xF];
  }
}

void BinToHex(char fmt, const std::string& binary, std::string* hex) {
  hex->resize(binary.size() * 2);
  char* hexdata = &(*hex->begin());
  for (size_t i = 0; i < binary.size(); i++) {
    CharToHex(fmt, binary.at(i), hexdata + 2 * i);
  }
}

void BinToHex(char fmt, const char* binary, int nbytes, std::string* hex) {
  hex->resize(nbytes * 2);
  char* hexdata = &(*hex->begin());
  for (int i = 0; i < nbytes; i++) {
    CharToHex(fmt, binary[i], hexdata + 2 * i);
  }
}


inline char HexToChar(const char hex[2])  {
  char ret = 0;
  if (hex[0] >= '0' && hex[0] <= '9') {
    ret |= ((hex[0] - '0') << 4) & 0xF0;
  } else if (hex[0] >= 'a' && hex[0] <= 'f') {
    ret |= ((hex[0] - 'a' + 10) << 4) & 0xF0;
  } else {
    assert(hex[0] >= 'A' && hex[0] <= 'F');
    ret |= ((hex[0] - 'A' + 10) << 4) & 0xF0;
  }

  if (hex[1] >= '0' && hex[1] <= '9') {
    ret |= (hex[1] - '0') & 0xF;
  } else if (hex[1] >= 'a' && hex[1] <= 'f') {
    ret |= (hex[1] - 'a' + 10) & 0xF;
  } else {
    assert(hex[1] >= 'A' && hex[1] <= 'F');
    ret |= (hex[1] - 'A' + 10) & 0xF;
  }

  return ret;
}

void HexToBin(const std::string& hex, std::string* bin) {
  bin->resize(hex.size() >> 1);
  const char* hexdata = hex.data();
  char* bindata = &(*bin->begin());
  for (size_t i = 0; i < bin->size(); i++) {
    bindata[i] = HexToChar(hexdata + 2 * i);
  }
}

std::string ConvertBase16To32(std::string number, std::string from_base, std::string to_base) {
	if (strcmp(from_base.c_str(), to_base.c_str()) == 0) {
		return number;
	}
	size_t from_base_len = from_base.size();
	assert(from_base_len == 16);
	size_t to_base_len = to_base.size();
	assert(to_base_len == 32);
	size_t number_len = number.size();

	std::string out;
	for (int i = number_len - 1; i >= 9; i -= 10) {
		unsigned long long bits = 0;
		for (int j = 9; j >= 0; j--) {
			if (j != 9) {
				bits <<= 4;
			}
			size_t value = from_base.find(number.at(i-j));
      if (value == std::string::npos) {
        return "";
      }

			bits += value;
		}
		unsigned long long mask = 0;
		unsigned long long index = 0;
		for (int k = 0; k < 40; k += 5) {
			mask = 0x1f;
			mask <<= k;
			index = bits & mask;
			index >>= k;
			out.insert(0, 1, to_base.at(static_cast<int>(index)));
		}
	}

	int len = number_len % 10;
	unsigned long long bits = 0;
	for (int j = 0; j < len; j++) {
		if (j != 0) {
			bits <<= 4;
		}
		size_t value = from_base.find(number.at(j));
    if (value == std::string::npos) {
      return "";
    }
		bits += value;
	}
	unsigned long long mask = 0;
	unsigned long long index = 0;
	for (int k = 0; k < len * 4; k += 5) {
		mask = 0x1f;
		mask <<= k;
		index = bits & mask;
		index >>= k;
		out.insert(0, 1, to_base.at(static_cast<int>(index)));
	}

	return out;
}

std::string ConvertBase32To16(std::string number, std::string from_base,
                             std::string to_base) {
	size_t from_base_len = from_base.size();
	assert(from_base_len == 32);
	size_t to_base_len = to_base.size();
	assert(to_base_len == 16);
	size_t number_len = number.size();

	std::string out;
	for (int i = number_len - 1; i >= 7; i -= 8) {
		unsigned long long bits = 0;
		for (int j = 7; j >= 0; j--) {
			if (j != 7) {
				bits <<= 5;
			}
			size_t value = from_base.find(number.at(i-j));
      if (value == std::string::npos) {
        return "";
      }
			bits += value;
		}
		unsigned long long mask = 0;
		unsigned long long index = 0;
		for (int k = 0; k < 40; k += 4) {
			mask = 0x0f;
			mask <<= k;
			index = bits & mask;
			index >>= k;
			out.insert(0, 1, to_base.at(static_cast<int>(index)));
		}
	}

	int len = number_len % 8;
	unsigned long long bits = 0;
	for (int j = 0; j < len; j++) {
		if (j != 0) {
			bits <<= 5;
		}
		size_t value = from_base.find(number.at(j));
    if (value == std::string::npos) {
      return "";
    }
		bits += value;
	}
	unsigned long long mask = 0;
	unsigned long long index = 0;
	for (int k = 0; k + 4 < len * 5; k += 4) {
		mask = 0x0f;
		mask <<= k;
		index = bits & mask;
		index >>= k;
		out.insert(0, 1, to_base.at(static_cast<int>(index)));
	}

	return out;
}

const std::string salt = "R&Y7Y4U6#KY^MBKJ";
const std::string base32 = "ABCDEFGHIJKLMNOPQRSTUV0123456789";
const std::string base16 = "0123456789abcdef";

bool VerifyKey(const std::string &input) {
  std::string temp = input;
  for (auto it = temp.begin(); it != temp.end(); ) {
    if (*it == '-') {
      it = temp.erase(it);
    } else {
      ++it;
    }
  }

  if (temp.size() != 30) {
    return false;
  }

  std::string out16 = ConvertBase32To16(temp, base32, base16);
  if (out16.empty()) {
    return false;
  }
  std::string md5str = out16.substr(0, 28);
  std::string seed_salt = out16.substr(28, 9) + salt;
  std::string md5_varify;
  Md5Hex(seed_salt, &md5_varify);
  
  return (strcmp(md5str.c_str(), md5_varify.substr(0, 28).c_str()) == 0);
}

}  // namespace zs
