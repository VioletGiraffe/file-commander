#pragma once

#define SECTION_WITH_AUTO_NAME SECTION(STRINGIFY_EXPANDED_ARGUMENT(__LINE__))
#define TRACE_LOG Logger() << Catch::getResultCapture().getCurrentTestName() << ",\tline " << __LINE__ << '\t'