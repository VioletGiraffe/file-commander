#ifndef FILE_OPERATIONS_TEST_HOOKS
#error This file must only be compiled into test builds - fileoperations.pri excludes it when the define is absent.
#endif

#include "operationtesthooks.h"

#include "assert/advanced_assert.h"

#include <array>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <utility>
#include <vector>

namespace OperationTestHooks
{

namespace
{

struct PointState
{
	std::optional<thin_io::filesystem_error_code> forcedError;
	bool forcedErrorConsumed = false;
	bool barrierArmed = false;
	bool barrierReached = false;
	bool barrierReleased = false;
	bool workerBlockedAtBarrier = false;
	uint32_t arrivals = 0;
};

std::mutex g_mutex;
std::condition_variable g_stateChanged;
std::array<PointState, static_cast<size_t>(Point::PointCount_)> g_state;
bool g_scopeActive = false;

std::function<void(const std::string&)> g_violationReporter = [](const std::string& message) {
	std::cerr << "OperationTestHooks violation: " << message << '\n';
};

PointState& stateFor(const Point point)
{
	const auto index = static_cast<size_t>(point);
	assert_r(index < g_state.size());
	return g_state[index];
}

std::string pointName(const Point point)
{
	switch (point)
	{
	case Point::SelfTest1: return "SelfTest1";
	case Point::SelfTest2: return "SelfTest2";
	case Point::RenameEntry_Native: return "RenameEntry_Native";
	case Point::RemoveEntry_Native: return "RemoveEntry_Native";
	case Point::CreateDirectory_FinalNative: return "CreateDirectory_FinalNative";
	case Point::SetEntryWritable_Native: return "SetEntryWritable_Native";
	case Point::PointCount_: break;
	}
	return "<invalid point>";
}

} // namespace

std::optional<thin_io::filesystem_error_code> fireHook(const Point point)
{
	std::unique_lock lock{g_mutex};
	if (!g_scopeActive)
		return {};

	auto& state = stateFor(point);
	++state.arrivals;

	if (state.barrierArmed)
	{
		state.barrierReached = true;
		state.workerBlockedAtBarrier = true;
		g_stateChanged.notify_all();
		g_stateChanged.wait(lock, [&state] { return state.barrierReleased || !g_scopeActive; });
		state.workerBlockedAtBarrier = false;
		g_stateChanged.notify_all();
	}

	if (state.forcedError && !state.forcedErrorConsumed)
	{
		state.forcedErrorConsumed = true;
		return state.forcedError;
	}

	return {};
}

CFaultHookScope::CFaultHookScope()
{
	std::lock_guard lock{g_mutex};
	assert_r(!g_scopeActive);
	g_scopeActive = true;
}

CFaultHookScope::~CFaultHookScope()
{
	std::vector<std::string> violations;

	{
		std::unique_lock lock{g_mutex};

		for (size_t i = 0; i < g_state.size(); ++i)
		{
			const auto& state = g_state[i];
			const auto name = pointName(static_cast<Point>(i));
			if (state.forcedError && !state.forcedErrorConsumed)
				violations.emplace_back("forced error at " + name + " was never consumed");
			if (state.barrierArmed && !state.barrierReached)
				violations.emplace_back("barrier at " + name + " was never reached");
			if (state.workerBlockedAtBarrier)
				violations.emplace_back("a worker was still blocked at " + name + " at scope teardown");
		}

		// Release blocked workers and wait for them to leave before wiping the state they reference.
		g_scopeActive = false;
		g_stateChanged.notify_all();
		g_stateChanged.wait(lock, [] {
			for (const auto& state : g_state)
			{
				if (state.workerBlockedAtBarrier)
					return false;
			}
			return true;
		});

		g_state = {};
	}

	for (const auto& violation : violations)
		g_violationReporter(violation);
}

void CFaultHookScope::forceNativeError(const Point point, const thin_io::filesystem_error_code code)
{
	std::lock_guard lock{g_mutex};
	auto& state = stateFor(point);
	assert_r(!state.forcedError);
	state.forcedError = code;
}

void CFaultHookScope::armBarrier(const Point point)
{
	std::lock_guard lock{g_mutex};
	auto& state = stateFor(point);
	assert_r(!state.barrierArmed);
	state.barrierArmed = true;
}

bool CFaultHookScope::waitForBarrier(const Point point, const std::chrono::milliseconds timeout) const
{
	std::unique_lock lock{g_mutex};
	return g_stateChanged.wait_for(lock, timeout, [&] { return stateFor(point).barrierReached; });
}

void CFaultHookScope::releaseBarrier(const Point point)
{
	std::lock_guard lock{g_mutex};
	stateFor(point).barrierReleased = true;
	g_stateChanged.notify_all();
}

uint32_t CFaultHookScope::arrivalCount(const Point point) const
{
	std::lock_guard lock{g_mutex};
	return stateFor(point).arrivals;
}

bool CFaultHookScope::forcedErrorConsumed(const Point point) const
{
	std::lock_guard lock{g_mutex};
	return stateFor(point).forcedErrorConsumed;
}

std::function<void(const std::string&)> CFaultHookScope::setViolationReporter(std::function<void(const std::string&)> reporter)
{
	std::lock_guard lock{g_mutex};
	assert_r(reporter);
	return std::exchange(g_violationReporter, std::move(reporter));
}

} // namespace OperationTestHooks
