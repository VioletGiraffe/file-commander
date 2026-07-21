// Tests for the OperationTestHooks facility itself, driven through its two dedicated self-test points.
// Every filesystem-fault test in this project depends on the behaviors proven here.

#include "fileoperations/operationtesthooks.h"

#include "fileoperationtesthelpers.h"

#include "lang/utils.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace OperationTestHooks;
using namespace std::chrono_literals;

namespace
{

constexpr thin_io::filesystem_error_code testErrorCode = 12345;

// Captures violations instead of failing the test, for tests that deliberately trigger them.
struct ViolationCapture
{
	ViolationCapture()
	{
		previousReporter = CFaultHookScope::setViolationReporter([this](const std::string& message) {
			violations.push_back(message);
		});
	}

	~ViolationCapture()
	{
		CFaultHookScope::setViolationReporter(mv(previousReporter));
	}

	std::vector<std::string> violations;
	std::function<void(const std::string&)> previousReporter;
};

} // namespace

TEST_CASE("Test hooks: unconfigured points pass through", "[testhooks]")
{
	CFaultHookScope scope;
	CHECK(!fireHook(Point::SelfTest1).has_value());
	CHECK(!fireHook(Point::SelfTest2).has_value());
	CHECK(scope.arrivalCount(Point::SelfTest1) == 1);
	CHECK(scope.arrivalCount(Point::SelfTest2) == 1);
}

TEST_CASE("Test hooks: firing with no active scope is a no-op", "[testhooks]")
{
	CHECK(!fireHook(Point::SelfTest1).has_value());
}

TEST_CASE("Test hooks: forced error is one-shot and per-point", "[testhooks]")
{
	CFaultHookScope scope;
	scope.forceNativeError(Point::SelfTest1, testErrorCode);

	// The other point is unaffected.
	CHECK(!fireHook(Point::SelfTest2).has_value());

	const auto forced = fireHook(Point::SelfTest1);
	REQUIRE(forced.has_value());
	CHECK(*forced == testErrorCode);
	CHECK(scope.forcedErrorConsumed(Point::SelfTest1));

	// One-shot: the next arrival proceeds normally.
	CHECK(!fireHook(Point::SelfTest1).has_value());
	CHECK(scope.arrivalCount(Point::SelfTest1) == 2);
}

TEST_CASE("Test hooks: concurrent workers consume a one-shot exactly once", "[testhooks]")
{
	CFaultHookScope scope;
	scope.forceNativeError(Point::SelfTest1, testErrorCode);

	constexpr int nWorkers = 8;
	std::vector<std::future<bool>> results;
	results.reserve(nWorkers);
	for (int i = 0; i < nWorkers; ++i)
		results.push_back(std::async(std::launch::async, [] { return fireHook(Point::SelfTest1).has_value(); }));

	int nForced = 0;
	for (auto& result : results)
		nForced += result.get() ? 1 : 0;

	CHECK(nForced == 1);
	CHECK(scope.arrivalCount(Point::SelfTest1) == nWorkers);
	CHECK(scope.forcedErrorConsumed(Point::SelfTest1));
}

TEST_CASE("Test hooks: barrier blocks the worker until released", "[testhooks]")
{
	CFaultHookScope scope;
	scope.armBarrier(Point::SelfTest1);

	std::atomic<bool> passed = false;
	std::thread worker([&passed] {
		(void)fireHook(Point::SelfTest1);
		passed = true;
	});

	REQUIRE(scope.waitForBarrier(Point::SelfTest1, 5s));
	// The worker is at the barrier, not past it.
	std::this_thread::sleep_for(50ms);
	CHECK(!passed);

	scope.releaseBarrier(Point::SelfTest1);
	worker.join();
	CHECK(passed);
}

TEST_CASE("Test hooks: barrier and forced error compose on one point", "[testhooks]")
{
	CFaultHookScope scope;
	scope.armBarrier(Point::SelfTest1);
	scope.forceNativeError(Point::SelfTest1, testErrorCode);

	auto result = std::async(std::launch::async, [] { return fireHook(Point::SelfTest1); });

	REQUIRE(scope.waitForBarrier(Point::SelfTest1, 5s));
	scope.releaseBarrier(Point::SelfTest1);

	const auto forced = result.get();
	REQUIRE(forced.has_value());
	CHECK(*forced == testErrorCode);
}

TEST_CASE("Test hooks: waitForBarrier times out when the point is not reached", "[testhooks]")
{
	ViolationCapture capture;
	{
		CFaultHookScope scope;
		scope.armBarrier(Point::SelfTest1);
		CHECK(!scope.waitForBarrier(Point::SelfTest1, 10ms));
		scope.releaseBarrier(Point::SelfTest1);
	}
	// The armed barrier was never reached - teardown must report it.
	REQUIRE(capture.violations.size() == 1);
	CHECK(capture.violations.front().find("never reached") != std::string::npos);
}

TEST_CASE("Test hooks: teardown reports a missed forced error", "[testhooks]")
{
	ViolationCapture capture;
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::SelfTest1, testErrorCode);
	}
	REQUIRE(capture.violations.size() == 1);
	CHECK(capture.violations.front().find("never consumed") != std::string::npos);
}

TEST_CASE("Test hooks: scope teardown resets all state", "[testhooks]")
{
	ViolationCapture capture; // The deliberately missed configurations must not fail the test.
	{
		CFaultHookScope scope;
		scope.forceNativeError(Point::SelfTest1, testErrorCode);
		scope.armBarrier(Point::SelfTest2);
	}
	{
		CFaultHookScope scope;
		CHECK(!fireHook(Point::SelfTest1).has_value()); // No forced error left over.
		CHECK(!fireHook(Point::SelfTest2).has_value()); // No barrier left over: would block forever here.
		CHECK(scope.arrivalCount(Point::SelfTest1) == 1); // Arrival counts restarted.
	}
	CHECK(capture.violations.size() == 2);
}

TEST_CASE("Test hooks: teardown releases a still-blocked worker and reports it", "[testhooks]")
{
	ViolationCapture capture;
	std::thread worker;
	{
		CFaultHookScope scope;
		scope.armBarrier(Point::SelfTest1);
		worker = std::thread([] { (void)fireHook(Point::SelfTest1); });
		REQUIRE(scope.waitForBarrier(Point::SelfTest1, 5s));
		// Deliberately no releaseBarrier(): teardown must unblock the worker itself.
	}
	worker.join();
	REQUIRE(capture.violations.size() == 1);
	CHECK(capture.violations.front().find("still blocked") != std::string::npos);
}
