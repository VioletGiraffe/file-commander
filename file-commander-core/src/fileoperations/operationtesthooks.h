#pragma once

// Deterministic fault / barrier seam for the file-operation module.
//
// The module's primitives call fireHook() at named points placed immediately around their native mutation calls.
// The direct-source test build defines FILE_OPERATIONS_TEST_HOOKS and drives the points through CFaultHookScope:
// a point can return a forced native error code (classified by the primitive exactly as a real native failure),
// or block at a barrier until the test has changed filesystem state and releases it.
// Without the define, fireHook() is an empty inline stub and the checks compile out of production code.

#include "filesystem_error.hpp" // thin_io

#include <optional>
#include <stdint.h>

#ifdef FILE_OPERATIONS_TEST_HOOKS
#include <chrono>
#include <functional>
#include <string>
#endif

namespace OperationTestHooks
{

// One value per named boundary. Points are added alongside the primitive code that consults them.
enum class Point : uint32_t
{
	// Exercised only by the facility's own tests, never by module code.
	SelfTest1,
	SelfTest2,

	PointCount_ // Not a point - registry size marker.
};

#ifdef FILE_OPERATIONS_TEST_HOOKS

// Called by module primitives at each named boundary. With no active CFaultHookScope, or no configuration
// for the point, returns nullopt immediately. Blocks while a barrier is armed at the point.
// A returned code means "the native call failed with this error": the primitive must skip the real call
// and classify the code exactly as it would a real native failure.
[[nodiscard]] std::optional<thin_io::filesystem_error_code> fireHook(Point point);

// Test-side control over the hook registry. At most one instance may exist at a time; all hook state is
// reset on destruction. Teardown reports through the violation reporter: an armed forced error that was
// never consumed, a barrier that was never reached, and a worker still blocked at a barrier (released
// to prevent a deadlock).
class CFaultHookScope
{
public:
	CFaultHookScope();
	~CFaultHookScope();

	CFaultHookScope(const CFaultHookScope&) = delete;
	CFaultHookScope& operator=(const CFaultHookScope&) = delete;

	// One-shot: the next fireHook(point) consumes the code instead of performing the real native call.
	// Arming the same point twice within one scope is a usage error.
	void forceNativeError(Point point, thin_io::filesystem_error_code code);

	// fireHook(point) blocks until releaseBarrier(point). Arming twice is a usage error.
	void armBarrier(Point point);
	// Waits until a worker reaches the barrier (sticky: true even if it was already released past it).
	[[nodiscard]] bool waitForBarrier(Point point, std::chrono::milliseconds timeout) const;
	void releaseBarrier(Point point);

	// How many times fireHook(point) was called while this scope is active.
	[[nodiscard]] uint32_t arrivalCount(Point point) const;
	// Whether the one-shot forced error armed at the point was consumed.
	[[nodiscard]] bool forcedErrorConsumed(Point point) const;

	// The reporter outlives any scope; it is invoked once per violation during scope teardown.
	// Returns the previously installed reporter. The default writes to stderr.
	static std::function<void(const std::string&)> setViolationReporter(std::function<void(const std::string&)> reporter);
};

#else

inline std::optional<thin_io::filesystem_error_code> fireHook(Point) noexcept { return {}; }

#endif

} // namespace OperationTestHooks
