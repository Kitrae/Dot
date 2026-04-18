// =============================================================================
// Dot Engine - Job System Implementation
// =============================================================================

#include "Core/Jobs/JobSystem.h"

#include "Core/Log/Logger.h"

namespace Dot
{

JobSystem& JobSystem::Get()
{
    static JobSystem instance;
    return instance;
}

JobSystem::~JobSystem()
{
    if (m_Initialized.load(std::memory_order_acquire))
    {
        Shutdown();
    }
}

void JobSystem::Initialize(uint32 workerCount)
{
    if (m_Initialized.exchange(true, std::memory_order_acq_rel))
    {
        DOT_LOG_WARN("JobSystem", "Already initialized");
        return;
    }

    // Auto-detect worker count
    if (workerCount == 0)
    {
        workerCount = std::thread::hardware_concurrency();
        if (workerCount == 0)
        {
            workerCount = 4; // Fallback
        }
        // Leave one core for main thread
        workerCount = workerCount > 1 ? workerCount - 1 : 1;
    }

    DOT_LOG_INFO("JobSystem", "Initializing with %u worker threads", workerCount);

    m_ShuttingDown.store(false, std::memory_order_release);

    // Create worker threads
    m_Workers.reserve(workerCount);
    for (uint32 i = 0; i < workerCount; ++i)
    {
        m_Workers.emplace_back(&JobSystem::WorkerLoop, this, i);
    }
}

void JobSystem::Shutdown()
{
    if (!m_Initialized.load(std::memory_order_acquire))
    {
        return;
    }

    DOT_LOG_INFO("JobSystem", "Shutting down...");

    // Signal workers to stop
    m_ShuttingDown.store(true, std::memory_order_release);

    // Wake all workers
    m_WakeCondition.notify_all();

    // Join all workers
    for (auto& worker : m_Workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    m_Workers.clear();
    m_JobQueue.Clear();
    m_Initialized.store(false, std::memory_order_release);

    DOT_LOG_INFO("JobSystem", "Shutdown complete");
}

void JobSystem::Schedule(const Job& job)
{
    m_JobQueue.Push(job);
    m_WakeCondition.notify_one();
}

void JobSystem::ScheduleBatch(const Job* jobs, usize count)
{
    for (usize i = 0; i < count; ++i)
    {
        m_JobQueue.Push(jobs[i]);
    }
    m_WakeCondition.notify_all();
}

void JobSystem::WaitForCounter(JobCounter* counter)
{
    if (counter == nullptr)
    {
        return;
    }

    // Busy-wait with yielding
    // Could also help execute jobs while waiting
    while (!counter->IsComplete())
    {
        // Try to help by executing a job
        auto job = m_JobQueue.TryPop();
        if (job.has_value())
        {
            ExecuteJob(job.value());
        }
        else
        {
            std::this_thread::yield();
        }
    }
}

void JobSystem::WorkerLoop(uint32 workerId)
{
    DOT_LOG_DEBUG("JobSystem", "Worker %u started", workerId);

    while (!m_ShuttingDown.load(std::memory_order_acquire))
    {
        // Try to get a job
        auto job = m_JobQueue.TryPop();

        if (job.has_value())
        {
            ExecuteJob(job.value());
        }
        else
        {
            // No work available, wait for signal
            std::unique_lock<std::mutex> lock(m_WakeMutex);
            m_WakeCondition.wait_for(
                lock, std::chrono::milliseconds(10),
                [this]() { return m_ShuttingDown.load(std::memory_order_acquire) || !m_JobQueue.IsEmpty(); });
        }
    }

    DOT_LOG_DEBUG("JobSystem", "Worker %u stopped", workerId);
}

void JobSystem::ExecuteJob(const Job& job)
{
    if (job.function != nullptr)
    {
        job.function(job.data);
    }

    if (job.counter != nullptr)
    {
        job.counter->Decrement();
    }
}

} // namespace Dot
