// ParallelCopy.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <atomic>
#include <tuple>

#include "log.h"
CLog g_log(CLog::eS_DEBUG, "output.log");

#include "commandlineoptions.h"
#include "jobsystem.h"

std::atomic_size_t counter = 0;
std::atomic_size_t blocked = 0;
const size_t cas_iterations = 10000;

void cas_test()
{
	size_t iterations = cas_iterations;
	do
	{
		size_t expected = counter.load();
		size_t desired = expected + 1;
		while (!std::atomic_compare_exchange_weak(&counter, &expected, desired))
		{
			desired = expected + 1;
			blocked++;
		}
	} while (--iterations);
}

void Help()
{
	LOG_INFORMATION("ParallelCopy.exe [-t <threads>] [-h]");
	LOG_INFORMATION("--threads  -t  number of threads to use (default is (2*<cores>)-1)");
	LOG_INFORMATION("--help     -h  help");
}

int main(const int argc, const char* argv[])
{
#if defined(_DEBUG)
	std::string commandLineArgs = "";
	for (int index = 0; index < argc; ++index)
	{
		if (index > 0)
		{
			commandLineArgs += " ";
		}
		commandLineArgs += argv[index];
	}
	LOG_DEBUG("Command line [%s]", commandLineArgs.c_str());
#endif // defined(_DEBUG)

	struct SOptions
	{
		int m_numThreads = 0; // default number of threads
	} options;

	CCommandLineOptions opts(argc, argv, [&](int argc, const char* argv[], int& index) -> bool {
		return true;
	});
	opts.AddOption("threads", 't', [&](int argc, const char* argv[], int& index) -> bool {
		options.m_numThreads = atoi(argv[++index]);
		LOG_DEBUG("Threads [%s] => (%d)", argv[index], options.m_numThreads);
		return true;
	});
	opts.AddOption("help", 'h', [&](int argc, const char* argv[], int& index) -> bool {
		Help();
		return false;
	});

	if (opts.Parse())
	{
		CJobSystem jobSystem(options.m_numThreads);
		LOG_INFORMATION("Testing CAS using [%d] threads", jobSystem.NumThreads());

		size_t numJobs = jobSystem.NumThreads() * 4;
		size_t iterations = numJobs;
		do
		{
			jobSystem.AddJob([]() {
				cas_test();
			});
		} while (--iterations);

		size_t remaining = 0;
		size_t running = 0;
		DWORD sleepInterval = 50; // sleep interval of 50ms
		DWORD logInterval = 2000 / sleepInterval; // log interval of 2s
		DWORD logCounter = 0;
		do
		{
			remaining = jobSystem.JobCount();
			running = jobSystem.JobsRunning();
			if (++logCounter == logInterval)
			{
				logCounter = 0;
				LOG_INFORMATION("[%d] threads running; [%d] files remaining...", running, remaining);
			}

			jobSystem.Update();
			Sleep(sleepInterval);
		} while (remaining || running);

		LOG_INFORMATION("counter [%zu] (should be [%zu]); blocked [%zu]", counter.load(), numJobs * cas_iterations, blocked.load());
	}

	return 0;
}

