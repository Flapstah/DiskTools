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

void Help()
{
	LOG_INFORMATION("ParallelCopy.exe [-t <threads>] [-h] <manifest>");
	LOG_INFORMATION("--threads  -t  number of threads to use (default is (2*<cores>)-1)");
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
	opts.AddOption("help", 'h', [&](int argc, const char* argv[], int& index) -> bool {
		Help();
		return false;
	});

	if (opts.Parse())
	{
		// TODO: sanity check options after command line parsing
		LOG_INFORMATION("Copying files in [%s] and using [%d] threads", options.m_fileList, options.m_numThreads);
		CJobSystem jobSystem(options.m_numThreads);

		std::ifstream fileList(options.m_fileList);
		std::string line;
		size_t lineNumber = 1;
		while (std::getline(fileList, line))
		{
			size_t sep = line.find('|');
			if (sep == std::string::npos)
			{
				LOG_INFORMATION("Malformed line in [%s](%i) (should be 'src|dst' format)", options.m_fileList, lineNumber);
				break;
			}

			std::string source = line.substr(0, sep);
			std::string destination = line.substr(sep + 1);

			jobSystem.AddJob([source, destination]() {
				copyFile(source, destination);
			});

			++lineNumber;
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

