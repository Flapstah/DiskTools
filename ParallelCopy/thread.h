#pragma once

#include <limits>
#include <thread>
#include <string>

#include <Windows.h>
#undef max

class CThread
{
public:
	CThread(std::string& name)
		: m_name{name}
	{
	}

	template<typename functor>
	void Start(functor function, uint64_t affinityMask = std::numeric_limits<uint64_t>::max()) const
	{
		if (!m_thread.joinable())
		{
			if (SetThreadAffinityMask(m_thread.native_handle(), (DWORD_PTR)affinityMask) == 0)
			{
				LOG_DEBUG("[%s] (const) starting...", GetName())
				m_thread = std::thread(function);
			}
			else
			{
				LOG_DEBUG("[%s] unable to set affinity of [0x%X]", GetName(), affinityMask);
			}
		}
		else
		{
			LOG_DEBUG("[%s] already running...", GetName());
		}
	}

	template<typename functor>
	void Start(functor function, uint64_t affinityMask = std::numeric_limits<uint64_t>::max())
	{
		if (!m_thread.joinable())
		{
			if (SetThreadAffinityMask(m_thread.native_handle(), (DWORD_PTR)affinityMask) == 0)
			{
				LOG_DEBUG("[%s] starting...", GetName());
				m_thread = std::thread(function);
			}
			else
			{
				LOG_DEBUG("[%s] unable to set affinity of [0x%X]", GetName(), affinityMask);
			}
		}
		else
		{
			LOG_DEBUG("[%s] already running...", GetName());
		}
	}

	inline void Join()
	{
		if (m_thread.joinable())
		{
			m_thread.join();
		}
	}

	inline const char* GetName()
	{
		return m_name.c_str();
	}

	inline std::thread::id GetId()
	{
		return m_thread.get_id();
	}

protected:
private:
	std::string m_name;
	std::thread m_thread;
};
