#pragma once

#include "utility/macro_utils.h"

#define SECTION_WITH_AUTO_NAME SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__))
#define CURRENT_TEST_NAME Catch::getResultCapture().getCurrentTestName()
#define TRACE_LOG Logger() << CURRENT_TEST_NAME << ",\tline " << __LINE__ << '\t'
