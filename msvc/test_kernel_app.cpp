// test_kernel.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <UnitTest++/UnitTest++.h>

#include <memory>

#include "zisync_kernel.h"  // NOLINT
#include "zisync/kernel/zslog.h"

static std::unique_ptr<zs::IZiSyncKernel> kernel;
static zs::DefaultLogger logger("./Log");
const char app_path[] = "./";

int _tmain(int argc, _TCHAR* argv[])
{
	logger.Initialize();
	logger.error_to_stderr = true;
	LogInitialize(&logger);
	kernel.reset(zs::GetZiSyncKernel("actual"));
	UnitTest::RunAllTests();
	logger.CleanUp();

	getchar();

	return 0;
}
