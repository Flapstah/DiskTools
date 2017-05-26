// ParallelCopy.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "log.h"
CLog g_log(CLog::eS_DEBUG, "output.log");

#include "commandlineoptions.h"
#include "jobsystem.h"

void copyFile(const std::string& source, const std::string& destination)
{
	size_t length = MultiByteToWideChar(CP_UTF8, 0, destination.c_str(), (int)destination.length(), nullptr, 0);
	std::wstring path(length + 1, 0);
	MultiByteToWideChar(CP_UTF8, 0, destination.c_str(), (int)destination.length(), &path[0], (int)path.length());
	path[path.find_last_of(L'\\')] = 0; // trim file from path

	switch (SHCreateDirectoryEx(NULL, path.c_str(), nullptr))
	{
	case ERROR_ALREADY_EXISTS:
	case ERROR_FILE_EXISTS:
	case ERROR_SUCCESS:
		if (CopyFileExA(source.c_str(), destination.c_str(), nullptr, nullptr, nullptr, 0))
		{
			LOG_INFORMATION("Copied [%s]", destination.c_str());
		}
		else
		{
			LOG_INFORMATION("Failed to copy [%s] to [%s]: 0x%08X", source.c_str(), destination.c_str(), GetLastError());
		}
		break;
	default:
		LOG_INFORMATION("Failed to create parent directory for [%s]", destination.c_str());
		break;
	}
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
		const char* m_source = nullptr;
		const char* m_destination = nullptr;
		const char* m_fileList = nullptr;
		int m_numThreads = std::thread::hardware_concurrency() - 1;
	} options;

	CCommandLineOptions opts(argc, argv, [&](int argc, const char* argv[], int& index) -> bool {
		if (options.m_source == nullptr)
		{
			options.m_source = argv[index];
			LOG_DEBUG("Source [%s]", argv[index]);
		}
		else
		{
			options.m_destination = argv[index];
			LOG_DEBUG("Destination [%s]", argv[index]);
		}
		return true;
	});
	opts.AddOption("files", 'f', [&](int argc, const char* argv[], int& index) -> bool {
		options.m_fileList = argv[++index];
		LOG_DEBUG("Files option [%s]", argv[index]);
		return true;
	});
	opts.AddOption("threads", 't', [&](int argc, const char* argv[], int& index) -> bool {
		options.m_numThreads = atoi(argv[++index]);
		LOG_DEBUG("Threads [%s] => (%d)", argv[index], options.m_numThreads);
		return true;
	});

	if (opts.Parse())
	{
		// TODO: sanity check options after command line parsing
		LOG_INFORMATION("Copying from [%s] to [%s], directed by [%s] and using [%d] threads", options.m_source, options.m_destination, options.m_fileList, options.m_numThreads);
		CJobSystem jobSystem(options.m_numThreads);

		std::ifstream fileList(options.m_fileList);
		std::string file;
		while (std::getline(fileList, file))
		{
			std::string source = options.m_source;
			source += "\\";
			source += file;
			std::string destination = options.m_destination;
			destination += "\\";
			destination += file;

			jobSystem.AddJob([source, destination]() {
				copyFile(source, destination);
			});
		}

		while (jobSystem.JobCount())
		{
			jobSystem.Update();
			Sleep(50);
		}
	}
	else
	{
		LOG_INFORMATION("Muppet!");
	}
	return 0;
}

