#pragma once

#ifdef _WIN32
#include <Windows.h>
#elif defined(__linux__)
#include <sched.h>
#include <sys/resource.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <vector>

// A performance core at full clock: an efficiency core or power throttling would make the runs incomparable.
inline void pinToPerformanceCore()
{
#ifdef _WIN32
	ULONG length = 0;
	::GetSystemCpuSetInformation(nullptr, 0, &length, ::GetCurrentProcess(), 0);
	std::vector<uint8_t> buffer(length);
	if (length == 0 || !::GetSystemCpuSetInformation(reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer.data()), length, &length, ::GetCurrentProcess(), 0))
	{
		printf("Not pinned: no CPU set information\n");
		return;
	}

	// The last CPU of the highest efficiency class: a performance core, and far from CPU 0, which takes the most interrupts
	const SYSTEM_CPU_SET_INFORMATION* chosen = nullptr;
	for (ULONG offset = 0; offset < length;)
	{
		const auto* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
		if (info->Type == CpuSetInformation && info->CpuSet.Group == 0 && (!chosen || info->CpuSet.EfficiencyClass >= chosen->CpuSet.EfficiencyClass))
			chosen = info;

		offset += info->Size;
	}

	if (!chosen || !::SetThreadAffinityMask(::GetCurrentThread(), KAFFINITY{ 1 } << chosen->CpuSet.LogicalProcessorIndex))
	{
		printf("Not pinned: no usable CPU\n");
		return;
	}

	::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	THREAD_POWER_THROTTLING_STATE throttling{};
	throttling.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
	throttling.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
	throttling.StateMask = 0;
	::SetThreadInformation(::GetCurrentThread(), ThreadPowerThrottling, &throttling, sizeof(throttling));

	printf("Pinned to CPU %u\n", (unsigned)chosen->CpuSet.LogicalProcessorIndex);
#elif defined(__linux__)
	cpu_set_t allowed;
	CPU_ZERO(&allowed);
	if (::sched_getaffinity(0, sizeof(allowed), &allowed) != 0)
	{
		printf("Not pinned: no affinity mask\n");
		return;
	}

	// The highest allowed CPU. Uniform cores make it as good as any; a hybrid x86 machine, where sysfs does not
	// expose which cores are the fast ones, needs taskset instead.
	int chosen = -1;
	for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
	{
		if (CPU_ISSET(cpu, &allowed))
			chosen = cpu;
	}

	cpu_set_t single;
	CPU_ZERO(&single);
	if (chosen >= 0)
		CPU_SET(chosen, &single);

	if (chosen < 0 || ::sched_setaffinity(0, sizeof(single), &single) != 0)
	{
		printf("Not pinned: no usable CPU\n");
		return;
	}

	// Fails without privileges, and only matters when something else wants the core
	(void)::setpriority(PRIO_PROCESS, 0, -19);

	printf("Pinned to CPU %d\n", chosen);
#endif
}
