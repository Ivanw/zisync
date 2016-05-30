# nobase_include_HEADERS = sqlite3.h

# libsqlite3_la_SOURCES = sqlite3.c

# include_HEADERS = sqlite3.h


# test = TestUnitTest++
unittestdir = $(includedir)/UnitTest++
unittest_HEADERS = src/AssertException.h\
	src/CheckMacros.h\
	src/Checks.h\
	src/Config.h\
	src/CurrentTest.h\
	src/DeferredTestReporter.h\
	src/DeferredTestResult.h\
	src/ExecuteTest.h\
	src/MemoryOutStream.h\
	src/ReportAssert.h\
	src/Test.h\
	src/TestDetails.h\
	src/TestList.h\
	src/TestMacros.h\
	src/TestReporter.h\
	src/TestReporterStdout.h\
	src/TestResults.h\
	src/TestRunner.h\
	src/TestSuite.h\
	src/TimeConstraint.h\
	src/TimeHelpers.h\
	src/UnitTest++.h\
	src/XmlTestReporter.h
posixdir = $(unittestdir)/Posix
posix_HEADERS = src/Posix/SignalTranslator.h src/Posix/TimeHelpers.h

lib_LTLIBRARIES = libUnitTest++.la
libUnitTest___la_SOURCES = src/AssertException.cpp \
	src/Test.cpp \
	src/Checks.cpp \
	src/TestRunner.cpp \
	src/TestResults.cpp \
	src/TestReporter.cpp \
	src/TestReporterStdout.cpp \
	src/ReportAssert.cpp \
	src/TestList.cpp \
	src/TimeConstraint.cpp \
	src/TestDetails.cpp \
	src/MemoryOutStream.cpp \
	src/DeferredTestReporter.cpp \
	src/DeferredTestResult.cpp \
	src/XmlTestReporter.cpp \
	src/CurrentTest.cpp \
	src/Posix/SignalTranslator.cpp \
	src/Posix/TimeHelpers.cpp

# ifeq ($(MSYSTEM), MINGW32)
#   src += src/Win32/TimeHelpers.cpp
# else
#   src += src/Posix/SignalTranslator.cpp \
# 	src/Posix/TimeHelpers.cpp
# endif

check_PROGRAMS = TestUnitTest++
TestUnitTest___SOURCES = src/tests/Main.cpp \
	src/tests/TestAssertHandler.cpp \
	src/tests/TestChecks.cpp \
	src/tests/TestUnitTest++.cpp \
	src/tests/TestTest.cpp \
	src/tests/TestTestResults.cpp \
	src/tests/TestTestRunner.cpp \
	src/tests/TestCheckMacros.cpp \
	src/tests/TestTestList.cpp \
	src/tests/TestTestMacros.cpp \
	src/tests/TestTimeConstraint.cpp \
	src/tests/TestTimeConstraintMacro.cpp \
	src/tests/TestMemoryOutStream.cpp \
	src/tests/TestDeferredTestReporter.cpp \
	src/tests/TestXmlTestReporter.cpp \
	src/tests/TestCurrentTest.cpp

TestUnitTest___LDADD = -lUnitTest++

TESTS = $(check_PROGRAMS)
