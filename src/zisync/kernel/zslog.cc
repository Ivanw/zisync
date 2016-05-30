/**
 * @file zslog.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Logging implmentation.
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
#include <string>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/platform/platform.h"
#include "zisync/kernel/database/icore.h"
#include "zisync/kernel/zslog.h"

namespace zs {

static ILogger* s_logger = NULL;

const char* const LogSeverityNames[NUM_SEVERITIES] = {
  "I", "W", "E", "F"
};

void LogInitialize(class ILogger* logger) {
  s_logger = logger;
}

void LogCleanUp() {
}

void RawLog(int severity, const char* file, int line, const char* format, ...) {
assert(s_logger != NULL);
  if (s_logger) {
    int thread_id = OsGetThreadId();

    char datetime[25];
    OsGetTimeString(datetime, 25);

    std::string message;

    va_list ap;
    va_start(ap, format);
    StringFormatV(&message, format, ap);
    va_end(ap);

    s_logger->AppendToLog(
        severity, file, line, datetime, thread_id, message.data(),
        message.size());
  }
}


}  // namespace zs

