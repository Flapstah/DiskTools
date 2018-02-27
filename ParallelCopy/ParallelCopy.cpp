// ParallelCopy.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <atomic>
#include <tuple>

#include "log.h"
CLog g_log(CLog::eS_DEBUG, "output.log");

#include "commandlineoptions.h"
#include "jobsystem.h"

volatile std::atomic_size_t failedToCopy = 0;
volatile unsigned int MAX_RETRIES = 10;
volatile DWORD RETRY_DELAY = 10000; // 10 second retry delay

void copyFile(const std::string& source, const std::string& destination)
{
	size_t length = MultiByteToWideChar(CP_UTF8, 0, destination.c_str(), (int)destination.length(), nullptr, 0);
	std::wstring path(length + 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, destination.c_str(), (int)destination.length(), &path[0], (int)path.length());
	path[path.find_last_of(L"/\\")] = 0; // trim file from path

	bool copied = false;
	DWORD retries = MAX_RETRIES;

	switch (SHCreateDirectoryEx(NULL, path.c_str(), nullptr))
	{
	case ERROR_ALREADY_EXISTS:
	case ERROR_FILE_EXISTS:
	case ERROR_SUCCESS:
		while (!copied && retries)
		{
			if (CopyFileExA(source.c_str(), destination.c_str(), nullptr, nullptr, nullptr, 0/*COPY_FILE_NO_BUFFERING*/))
			{
				if (retries != MAX_RETRIES)
				{
					LOG_INFORMATION("Copied [%s] to [%s] after [%d] retries", source.c_str(), destination.c_str(), MAX_RETRIES - retries);
				}
				copied = true;
			}
			else
			{
				//LOG_ERROR("Failed to copy [%s] to [%s]; [%d] retries remaining: GetLastError() 0x%08X; sleeping before retry", source.c_str(), destination.c_str(), retries, GetLastError());
				--retries;
				Sleep(RETRY_DELAY);
			}
		}

		if (!copied)
		{
			LOG_ERROR("Failed to copy [%s] to [%s] after [%d] retries: GetLastError() 0x%08X", source.c_str(), destination.c_str(), MAX_RETRIES, GetLastError());
			++failedToCopy;
		}

		break;
	case ERROR_BAD_PATHNAME:
		LOG_ERROR("Bad pathname [%s]", path.c_str());
		++failedToCopy;
		break;
	case ERROR_FILENAME_EXCED_RANGE:
		LOG_ERROR("Pathname [%s] too long", path.c_str());
		++failedToCopy;
		break;
	case ERROR_CANCELLED:
		LOG_WARNING("User cancelled creating directory [%s]", path.c_str());
		++failedToCopy;
		break;
	default:
		LOG_INFORMATION("Failed to create parent directory for [%s]", destination.c_str());
		++failedToCopy;
		break;
	}
}

void Help()
{
	LOG_INFORMATION("ParallelCopy.exe [-t <threads>] [-h] <manifest>");
	LOG_INFORMATION("--threads  -t  number of threads to use (default is (2*<cores>)-1)");
	LOG_INFORMATION("--max-retries  -r  maximum number of retries (default 10)");
	LOG_INFORMATION("--retry-delay  -d  delay (in ms) between retries (default 10000)");
	LOG_INFORMATION("--help     -h  help");
	LOG_INFORMATION("<manifest>     a pipe seperated file list in the form src|dst, 1 entry per line");
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
		const char* m_fileList = nullptr;
		int m_numThreads = 0; // default number of threads
	} options;

	CCommandLineOptions opts(argc, argv, [&](int argc, const char* argv[], int& index) -> bool {
		options.m_fileList = argv[index];
		return true;
	});
	opts.AddOption("threads", 't', [&](int argc, const char* argv[], int& index) -> bool {
		options.m_numThreads = atoi(argv[++index]);
		LOG_DEBUG("Threads [%s] => (%d)", argv[index], options.m_numThreads);
		return true;
	});
	opts.AddOption("max-retries", 'r', [&](int argc, const char* argv[], int& index) -> bool {
		MAX_RETRIES = atoi(argv[++index]);
		LOG_DEBUG("Max retries [%s] => (%d)", argv[index], MAX_RETRIES);
		return true;
	});
	opts.AddOption("retry-delay", 'd', [&](int argc, const char* argv[], int& index) -> bool {
		RETRY_DELAY = atoi(argv[++index]);
		LOG_DEBUG("Retry delay [%sms] => (%dms)", argv[index], RETRY_DELAY);
		return true;
	});
	opts.AddOption("help", 'h', [&](int argc, const char* argv[], int& index) -> bool {
		Help();
		return false;
	});

	if (opts.Parse())
	{
		if (options.m_fileList != nullptr)
		{
			CJobSystem jobSystem(options.m_numThreads);
			LOG_INFORMATION("Copying files in [%s] and using [%d] threads (max retries [%d], retry delay [%dms])", options.m_fileList, jobSystem.NumThreads(), MAX_RETRIES, RETRY_DELAY);

			std::list<std::tuple<const std::string, const std::string>> files;
			std::ifstream fileList(options.m_fileList);
			std::string line;
			size_t count = 0;
			while (std::getline(fileList, line))
			{
				size_t sep = line.find('|');
				if (sep == std::string::npos)
				{
					LOG_ERROR("Malformed line in [%s](%i) (should be 'src|dst' format)", options.m_fileList, count + 1);
					break;
				}

				std::string source = line.substr(0, sep);
				std::string destination = line.substr(sep + 1);
				files.push_back(std::make_tuple(source, destination));
				++count;
			}

			for (const std::tuple<const std::string, const std::string>& copyOperation : files)
			{
				const std::string source = std::get<0>(copyOperation);
				const std::string destination = std::get<1>(copyOperation);
				//LOG_INFORMATION("Copying [%s] to [%s]...", source.c_str(), destination.c_str());

				jobSystem.AddJob([source, destination]() {
					copyFile(source, destination);
				});
			}

			size_t remaining = 0;
			size_t running = 0;
			DWORD sleepInterval = 50; // sleep interval of 50ms
			DWORD logInterval = 2000 / sleepInterval; // log interval of 2s
			DWORD logCounter = 0;
			bool working = true;
			while (working)
			{
				remaining = jobSystem.JobCount();
				running = jobSystem.JobsRunning();
				working = remaining || running;
				if (++logCounter == logInterval)
				{
					logCounter = 0;
					LOG_INFORMATION("[%d] threads running; [%d] files remaining...", running, remaining);
				}

				jobSystem.Update();
				Sleep(sleepInterval);
			}

			// Have to take local copies of atomics before passing to functions (can't access copy constructor)
			size_t failed = failedToCopy;
			LOG_INFORMATION("%d files copied, %d failed", count - failed, failed);
		}
		else
		{
			Help();
		}
	}

	return 0;
}

