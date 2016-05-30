/* Copyright [2014] <zisync.com> */
#ifndef ZISYNC_KERNEL_DATABASE_ICORE_H_
#define ZISYNC_KERNEL_DATABASE_ICORE_H_

#include <stdint.h>
#include <string>

#include "zisync_kernel.h"  // NOLINT

#define ARRAY_SIZE(pArray)  (sizeof(pArray)/sizeof((pArray)[0]))

namespace zs {


// __attribute__((format(printf, 1, 2)))
// const std::string StringFormat(const char* format, ...);
#if defined(__GNUC__) && (__GNUC__ >= 4)
__attribute__((format(printf, 2, 3)))
  int StringFormat(std::string* result, const char* format, ...);
__attribute__((format(printf, 2, 3)))
  int StringAppendFormat(std::string* result, const char* format, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
  int StringFormat(std::string* result, _Printf_format_string_ const char* format, ...);
  int StringAppendFormat(std::string* result, _Printf_format_string_ const char* format, ...);
# else
  int StringFormat(std::string* result, __format_string const char* format, ...);
  int StringAppendFormat(std::string* result, __format_string const char* format, ...);
# endif /* FORMAT_STRING */
#else
	void RawLog(int severity, const char* file, int line, const char* format, ...);
#endif


// int StringFormat(std::string* result, std::string format, ...);
int StringFormatV(std::string* result, const char* format, va_list ap);
// int StringFormatV(std::string* result, std::string format, va_list ap);
// int StringAppendFormat(std::string* result, std::string format, ...);
bool StringStartsWith(const char* str, const char* prefix);
bool StringStartsWith(const std::string& str, const std::string& prefix);
err_t StringImplode(
    std::string* buffer, const char* argv[], int argc, const char* glue);

/**
 * @param fmt: 'x' for lowercase, 'X' for upper case
 */
void CharToHex(const char fmt, char byte, char hex[2]);
void BinToHex(const char fmt, const std::string& binary, std::string* hex);
void BinToHex(const char fmt, const char* binary, int nbytes, std::string* hex);
char HexToChar(const char hex[2]);
void HexToBin(const std::string& hex, std::string* bin);
std::string ConvertBase16To32(std::string number, std::string from_base,
                              std::string to_base);
std::string ConvertBase32To16(std::string number, std::string from_base,
                              std::string to_base);
bool VerifyKey(const std::string &input);  

} // namespace zs

#endif  // ZISYNC_KERNEL_DATABASE_ICORE_H_
