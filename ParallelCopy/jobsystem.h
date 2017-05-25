#pragma once

// job system stuff
//#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "thread.h"

#define THREAD_ID "[" << std::this_thread::get_id() << "] "

class CJobSystem
{
public:
	enum EState : char
	{
		S_IDLE,
		S_RUNNING,
		S_SHUTTING_DOWN,
	};

	CJobSystem()
		: m_numThreads{std::thread::hardware_concurrency() - 1}
	{
		LOG_INFORMATION("[%d] CJobSystem constructed : m_numThreads = %d", std::this_thread::get_id(), m_numThreads);
		CreateWorkerThreads();
	}

	CJobSystem(size_t numThreads)
		: m_numThreads{numThreads}
	{
		LOG_INFORMATION("[%d] CJobSystem constructed : m_numThreads = %d", std::this_thread::get_id(), m_numThreads);
		CreateWorkerThreads();
	}

	~CJobSystem()
	{
		Shutdown();
	}

	void Update()
	{
		// Main update loop to be run on the main thread
		// Needs to service callbacks on the main thread
		LOG_INFORMATION("[%d] CJobSystem::Update()", std::this_thread::get_id());
	}

	void Shutdown()
	{
		LOG_INFORMATION("[%d] CJobSystem::Shutdown()", std::this_thread::get_id());
		for (CWorkerThread*& workerThread : m_workerThreads)
		{
			if (workerThread != nullptr)
			{
				delete workerThread;
				workerThread = nullptr;
			}
		}
	}

private:
	void CreateWorkerThreads()
	{
		m_workerThreads.resize(m_numThreads);
		char nameBuffer[32] = "";
		for (size_t index = 0; index < m_numThreads; ++index)
		{
			sprintf_s(nameBuffer, sizeof(nameBuffer), "WorkerThread%d", index);
			std::string name(nameBuffer);
			m_workerThreads[index] = new CWorkerThread(name, &m_jobQueue, index);
			LOG_INFORMATION("[%d] CJobSystem::CreateWorkerThreads() created [%d]", std::this_thread::get_id(), m_workerThreads[index]->GetId());
		}
	}

	class CJobQueue
	{
	public:
		size_t size()
		{
			return m_queue.size();
		}

		void push(std::function<void()>&& function, uint64_t&& affinityMask = std::numeric_limits<uint64_t>::max())
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_queue.push_back(SJobInfo(std::forward<std::function<void()>>(function), std::forward<uint64_t>(affinityMask)));
		}

		std::function<void()>&& pop()
		{
			static std::function<void()> none = nullptr;
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_queue.empty())
			{
				std::function<void()>&& outFunction = std::move(m_queue.front().m_function);
				m_queue.pop_front();
				return std::move(outFunction);
			}
			return std::move(none);
		}

	private:
		struct SJobInfo
		{
			SJobInfo(std::function<void()>&& function, uint64_t&& affinityMask)
				: m_affinityMask(affinityMask)
				, m_function(function)
			{
			}

			uint64_t m_affinityMask;
			std::function<void()> m_function;
		};

		std::mutex m_mutex;
		std::deque<SJobInfo> m_queue;
	};

	class CWorkerThread : public CThread
	{
	public:
		enum EState : char
		{
			S_IDLE,
			S_RUNNING,
			S_TERMINATING,
			S_TERMINATE,
		};

		CWorkerThread(std::string& name, CJobQueue* queue, uint64_t coreAffinity)
			: CThread{ name }
			, m_state{S_RUNNING}
			, m_queue{queue}
		{
			auto lambda = [this]() { this->Main(); };
			Start<decltype(lambda)>(lambda, 1LL << coreAffinity);
		}

		~CWorkerThread()
		{
			RequestTerminate();
		}

		void RequestTerminate()
		{
			LOG_INFORMATION("[%d] CWorkerThread::RequestTerminate() [%d]", std::this_thread::get_id(), GetId());
			if (GetState() == S_RUNNING)
			{
				SetState(S_TERMINATE);
				Join();
			}
		}

	private:
		void SetState(EState state)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			LOG_INFORMATION("[%d] CWorkerThread::SetState() [%d] %s -> %s", std::this_thread::get_id(), GetId(), GetStateString(m_state), GetStateString(state));
			m_state = state;
		}

#define CASE_ENUM_TO_STRING(_enum) case _enum : return #_enum
		inline const char* GetStateString(EState state)
		{
			switch (state)
			{
				CASE_ENUM_TO_STRING(S_IDLE);
				CASE_ENUM_TO_STRING(S_RUNNING);
				CASE_ENUM_TO_STRING(S_TERMINATING);
				CASE_ENUM_TO_STRING(S_TERMINATE);
			default:
				return "<UNKNOWN>";
			}
		}
		inline const EState GetState()
		{
			return m_state;
		}

		void Main()
		{
			LOG_INFORMATION("[%d] CWorkerThread::Main() starting", std::this_thread::get_id());

			while (GetState() == S_RUNNING)
			{
				std::function<void()>&& function = m_queue->pop();
				if (function != nullptr)
				{
					LOG_INFORMATION("[%d] CWorkerThread::Main() executing function", std::this_thread::get_id());
					function();
				}
				else
				{
					LOG_VERBOSE("[%d] CWorkerThread::Main() waiting", std::this_thread::get_id());
					//std::this_thread::yield();
					std::this_thread::sleep_for(std::chrono::microseconds(100));
				}
			}

			SetState(S_TERMINATING);
			LOG_INFORMATION("[%d] CWorkerThread::Main() shutting down with %d outstanding jobs in queue", std::this_thread::get_id(), m_queue->size());

			SetState(S_IDLE);
		}

		EState m_state;
		std::mutex m_mutex;
		CJobQueue* m_queue;
	};

	size_t m_numThreads;
	CJobQueue m_jobQueue;
	CJobQueue m_callbackQueue;
	std::vector<CWorkerThread*> m_workerThreads;
};


