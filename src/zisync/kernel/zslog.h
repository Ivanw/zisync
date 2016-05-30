/**
 * @file log.h
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief logging interface.
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

#ifndef ZISYNC_KERNEL_ZSLOG_H_
#define ZISYNC_KERNEL_ZSLOG_H_

namespace zs {

const int ZSLOG_INFO     = 0;
const int ZSLOG_WARNING  = 1;
const int ZSLOG_ERROR    = 2;
const int ZSLOG_FATAL    = 3;
const int NUM_SEVERITIES = 4;


extern const char* const LogSeverityNames[NUM_SEVERITIES];

// NDEBUG usage helpers related to (RAW_)DCHECK:
//
// DEBUG_MODE is for small !NDEBUG uses like
//   if (DEBUG_MODE) foo.CheckThatFoo();
// instead of substantially more verbose
//   #ifndef NDEBUG
//     foo.CheckThatFoo();
//   #endif
//
// IF_DEBUG_MODE is for small !NDEBUG uses like
//   IF_DEBUG_MODE( string error; )
//   DCHECK(Foo(&error)) << error;
// instead of substantially more verbose
//   #ifndef NDEBUG
//     string error;
//     DCHECK(Foo(&error)) << error;
//   #endif
//
#ifdef NDEBUG
enum { DEBUG_MODE = 0 };
#define IF_DEBUG_MODE(x)
#else
enum { DEBUG_MODE = 1 };
#define IF_DEBUG_MODE(x) x
#endif


// Usage example:
// 1. logging
//   ZSLOG(ERROR, "Failed foo with %i: %s", status, error);
//   ZSLOG(INFO, "status is %i", status);
// These will print an almost standard log lines like this to stderr only:
//   E0821 211317 file.cc:123] Failed foo with 22: bad_file
//   I0821 211317 file.cc:142] status is 20
//
// 2. check logging: logging if check failed.
//   ZSLOG_CHECK(var1 > var2, "check failed due to var2 > var1");

#define ZSLOG(severity, ...)                    \
  do {                                          \
    switch (zs::ZSLOG_ ## severity) {        \
      case 0:                                   \
        ZSLOG_INFO(__VA_ARGS__);              \
        break;                                  \
      case 1:                                   \
        ZSLOG_WARNING(__VA_ARGS__);           \
        break;                                  \
      case 2:                                   \
        ZSLOG_ERROR(__VA_ARGS__);             \
        break;                                  \
      case 3:                                   \
        ZSLOG_FATAL(__VA_ARGS__);             \
        break;                                  \
      default:                                  \
        break;                                  \
    }                                           \
  } while (0)

// The following STRIP_LOG testing is performed in the header file so that it's
// possible to completely compile out the logging code and the log messages.

#if STRIP_LOG == 0
#define ZSLOG_INFO(...) zs::RawLog(zs::ZSLOG_INFO,   \
  __FILE__, __LINE__, __VA_ARGS__)
#else
#define ZSLOG_INFO(...) zs::RawLogStub__(0, __VA_ARGS__)
#endif  // STRIP_LOG == 0

#if STRIP_LOG <= 1
#define ZSLOG_WARNING(...) zs::RawLog(zs::ZSLOG_WARNING, \
  __FILE__, __LINE__, __VA_ARGS__)
#else
#define ZSLOG_WARNING(...) zs::RawLogStub__(0, __VA_ARGS__)
#endif  // STRIP_LOG <= 1

#if STRIP_LOG <= 2
#define ZSLOG_ERROR(...) zs::RawLog(zs::ZSLOG_ERROR, \
  __FILE__, __LINE__, __VA_ARGS__)
#else
#define ZSLOG_ERROR(...) zs::RawLogStub__(0, __VA_ARGS__)
#endif  // STRIP_LOG <= 2

#if STRIP_LOG <= 3
#define ZSLOG_FATAL(...) zs::RawLog(zs::ZSLOG_FATAL, \
  __FILE__, __LINE__, __VA_ARGS__)
#else
#define ZSLOG_FATAL(...)                      \
  do {                                          \
    zs::RawLogStub__(0, __VA_ARGS__);           \
    exit(1);                                    \
  } while (0)
#endif  // STRIP_LOG <= 3

// Similar to CHECK(condition) << message,
// but for low-level modules: we use only ZSLOG that does not allocate memory.
// We do not want to provide args list here to encourage this usage:
//   if (!cond)  ZSLOG(FATAL, "foo ...", hard_to_compute_args);
// so that the args are not computed when not needed.
#define ZSLOG_CHECK(condition, message)                             \
  do {                                                              \
    if (!(condition)) {                                             \
      ZSLOG(FATAL, "Check %s failed: %s", #condition, message);   \
    }                                                               \
  } while (0)

// Debug versions of ZSLOG and ZSCHECK
#ifndef NDEBUG

#define ZSLOGD(severity, ...) ZSLOG(severity, __VA_ARGS__)
#define ZSCHECKD(condition, message) ZSCHECK(condition, message)

#else  // NDEBUG

#define ZSLOGD(severity, ...)                 \
  while (false)                                 \
    ZSLOG(severity, __VA_ARGS__)
#define ZSCHECKD(condition, message)          \
  while (false)                                 \
    ZSCHECK(condition, message)

#endif  // NDEBUG

// Stub log function used to work around for unused variable warnings when
// building with STRIP_LOG > 0.
static inline void RawLogStub__(int /* ignored */, ...) {
}

// function to implement ZSLOG
//
// Logs format... at "severity" level, reporting it as called from
// file:line.  This does not allocate memory or acquire locks.
#if defined(__GNUC__) && (__GNUC__ >= 4)
__attribute__((format(printf, 4, 5)))
	void RawLog(int severity, const char* file, int line, const char* format, ...);
#elif defined(_MSC_VER) && (_MSC_VER >= 1400)
# include <sal.h>
# if _MSC_VER > 1400
void RawLog(int severity, const char* file, int line, _Printf_format_string_ const char* format, ...);
# else
void RawLog(int severity, const char* file, int line, __format_string const char* format, ...);
# endif /* FORMAT_STRING */
#else
void RawLog(int severity, const char* file, int line, const char* format, ...);
#endif
}  // namespace zs

#endif  // ZISYNC_KERNEL_ZSLOG_H_
