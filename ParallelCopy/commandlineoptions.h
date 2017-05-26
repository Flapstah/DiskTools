#pragma once

#include "log.h"
#include <functional>
#include <list>
#include <string>

class CCommandLineOptions
{
public:
	CCommandLineOptions(int argc, const char* argv[], std::function<bool(int, const char*[], int&)>&& argumentParser)
		: m_argumentParser{ argumentParser }
		, m_argc{ argc }
		, m_argv{ argv }
	{
	}

	~CCommandLineOptions() {}

	void AddOption(const char* name, const char abbreviation, std::function<bool(int, const char*[], int&)>&& optionParser)
	{
		m_options.push_back(COption(std::string(name), abbreviation, std::move(optionParser)));
	}

	bool Parse(void)
	{
		bool ok = true;
		for (int index = 1; ok && (index < m_argc); ++index)
		{
			if (m_argv[index][0] == '-')
			{
				// Parse option
				bool parsed = false;
				for (COption& option : m_options)
				{
					if (	((m_argv[index][1] == '-') && (_stricmp(&m_argv[index][2], option.Name()) == 0)) ||	// Verbose
								((m_argv[index][1] == option.Abbreviation()) && (strlen(m_argv[index]) == 2))				// or terse
						)
					{
						ok = option.Parse(m_argc, m_argv, index);
						parsed = true;
						break;
					}
				}

				if (!parsed)
				{
					ok = false;
					LOG_ERROR("Option [%d] unknown: [%s]", index, m_argv[index]);
				}
			}
			else
			{
				// Parse argument
				ok = m_argumentParser(m_argc, m_argv, index);
			}
		}

		return ok;
	}

private:
	class COption
	{
	public:
		COption(std::string&& name, const char abbreviation, std::function<bool(int, const char*[], int&)>&& optionParser)
			: m_optionParser{ optionParser }
			, m_name{ name }
			, m_abbreviation{ abbreviation }
		{}
		~COption() {}

		const char* Name() { return m_name.c_str(); }
		const char Abbreviation() { return m_abbreviation; }
		bool Parse(int argc, const char* argv[], int& index) { return m_optionParser(argc, argv, index); }

	protected:
	private:
		std::function<bool(int, const char*[], int&)> m_optionParser;
		const std::string m_name;
		const char m_abbreviation;
	};

private:
	std::list<COption> m_options;
	std::function<bool(int, const char*[], int&)> m_argumentParser;
	const int m_argc;
	const char** m_argv;
};

