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

//
// TODO: for a thread worker pool with floating affinity, or a round-robin affinity pool, specifying thread affinity for a job is meaningless.
// If we're using a config file for threads with specific functions, then they should probably have their own job queues
//

class CJobSystem
{
public:
	enum EState : char
	{
		S_IDLE,
		S_RUNNING,
		S_SHUTTING_DOWN,
	};

	// Constructor that will provide a pool of job worker threads with either unique, or 'floating' thread affinity
	CJobSystem(size_t numThreads = 0, bool asFloatingPool = true)
		: m_numThreads{ numThreads }
	{
		if (m_numThreads == 0)
		{
			// General good practice is 2 software thread per hardware thread; the -1 is for the main thread
			m_numThreads = (std::thread::hardware_concurrency() * 2) - 1;
		}

		LOG_VERBOSE("[%d] CJobSystem constructed : m_numThreads = %d", std::this_thread::get_id(), m_numThreads);
		CreateWorkerThreads(asFloatingPool);
	}

	// Constructor that will create threads from the supplied thread config file
	CJobSystem(const std::string threadConfigFile)
		: m_numThreads{ 0 }
	{
		// TODO: Read config file then create threads; N.B. thread affinity
		LOG_VERBOSE("[%d] CJobSystem constructed : m_numThreads = %d", std::this_thread::get_id(), m_numThreads);
	}

	~CJobSystem()
	{
		Shutdown();
	}

	void Update()
	{
		// Main update loop to be run on the main thread, ensuring callbacks occur on the main thread
		LOG_VERBOSE("[%d] CJobSystem::Update()", std::this_thread::get_id());
		while (true)
		{
			std::function<void()>&& callback = m_callbackQueue.pop();
			if (callback == nullptr)
			{
				break;
			}
			else
			{
				LOG_VERBOSE("[%d] CJobSystem::Update(): servicing callback", std::this_thread::get_id());
				callback();
			}
		}
	}

	// TODO: create AddJob() with thread affinity
	void AddJob(std::function<void()>&& function)
	{
		m_jobQueue.push(std::forward<std::function<void()>>(function));
	}

	size_t JobCount()
	{
		return m_jobQueue.size();
	}

	void Shutdown()
	{
		LOG_VERBOSE("[%d] CJobSystem::Shutdown()", std::this_thread::get_id());
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
	// 'Floating' affinity means threads will be balanced on the cores according to core load
	// 'Unique' affinity locks threads to cores in a round-robin way.
	void CreateWorkerThreads(bool asFloatingPool)
	{
		m_workerThreads.resize(m_numThreads);
		size_t affinityMax = std::thread::hardware_concurrency();
		char nameBuffer[32] = "";
		for (size_t index = 0; index < m_numThreads; ++index)
		{
			sprintf_s(nameBuffer, sizeof(nameBuffer), "WorkerThread%zd", index);
			std::string name(nameBuffer);
			m_workerThreads[index] = (asFloatingPool) ? new CWorkerThread(name, &m_jobQueue) : new CWorkerThread(name, &m_jobQueue, 1LL << (index % affinityMax));
			LOG_DEBUG("[%d] CJobSystem::CreateWorkerThreads() created thread #%d [%d]", std::this_thread::get_id(), index, m_workerThreads[index]->GetId());
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
			LOG_VERBOSE("[%d] CJobQueue::push() Adding job to jobqueue", std::this_thread::get_id());
			m_queue.push_back(SJobInfo(std::forward<std::function<void()>>(function), std::forward<uint64_t>(affinityMask)));
		}

		// TODO: pop needs to consider job thread affinity
		std::function<void()> pop()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_queue.empty())
			{
				LOG_VERBOSE("[%d] CJobQueue::pop() Removing job from jobqueue", std::this_thread::get_id());
				std::function<void()> outFunction(std::move(m_queue.front().m_function));
				m_queue.pop_front();
				return outFunction;
			}
			LOG_VERBOSE("[%d] CJobQueue::pop() Jobqueue empty", std::this_thread::get_id());
			return nullptr;
		}

	private:
		struct SJobInfo
		{
			SJobInfo(std::function<void()>&& function, uint64_t&& affinityMask)
				: m_affinityMask{ std::forward<uint64_t>(affinityMask) }
				, m_function{ std::forward<std::function<void()>>(function) }
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

		// Worker thread with 'floating' core affinity
		CWorkerThread(std::string& name, CJobQueue* queue)
			: CThread{ name }
			, m_state{ S_RUNNING }
			, m_queue{ queue }
		{
			auto lambda = [this]() { this->Main(); };
			Start<decltype(lambda)>(lambda);
		}

		// Worker thread with specified core affinity
		CWorkerThread(std::string& name, CJobQueue* queue, uint64_t coreAffinity)
			: CThread{ name }
			, m_state{ S_RUNNING }
			, m_queue{ queue }
		{
			auto lambda = [this]() { this->Main(); };
			Start<decltype(lambda)>(lambda, coreAffinity);
		}

		~CWorkerThread()
		{
			RequestTerminate();
		}

		void RequestTerminate()
		{
			LOG_VERBOSE("[%d] CWorkerThread::RequestTerminate() [%d]", std::this_thread::get_id(), GetId());
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
			LOG_VERBOSE("[%d] CWorkerThread::SetState() [%d] %s -> %s", std::this_thread::get_id(), GetId(), GetStateString(m_state), GetStateString(state));
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

		//TODO: worker threads need to check for jobs for the correct affinity (need to think about this)
		void Main()
		{
			LOG_VERBOSE("[%d] CWorkerThread::Main() starting", std::this_thread::get_id());

			while (GetState() == S_RUNNING)
			{
				std::function<void()> function(m_queue->pop());
				if (function != nullptr)
				{
					LOG_VERBOSE("[%d] CWorkerThread::Main() executing function", std::this_thread::get_id());
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
			LOG_DEBUG("[%d] CWorkerThread::Main() shutting down with %d outstanding jobs in queue", std::this_thread::get_id(), m_queue->size());

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


