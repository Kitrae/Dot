// =============================================================================
// Dot Engine - Job System
// =============================================================================
// Manages worker threads and job scheduling. Call Schedule() to add work,
// workers automatically execute jobs as they become available.
// =============================================================================

#pragma once

#include "Core/Jobs/Job.h"
#include "Core/Jobs/JobQueue.h"

#include <atomic>
#include <condition_variable>
#include <thread>
#include <vector>


namespace Dot
{

/// Job System - manages worker threads and job execution
///
/// Usage:
/// @code
///     // Initialize with N worker threads
///     JobSystem::Get().Initialize(4);
///
///     // Schedule work
///     JobCounter counter(3);
///     JobSystem::Get().Schedule(Job::Create(MyWork, data, &counter));
///     JobSystem::Get().Schedule(Job::Create(MyWork, data, &counter));
///     JobSystem::Get().Schedule(Job::Create(MyWork, data, &counter));
///
///     // Wait for completion
///     JobSystem::Get().WaitForCounter(&counter);
///
///     // Shutdown
///     JobSystem::Get().Shutdown();
/// @endcode
class DOT_CORE_API JobSystem
{
public:
    /// Get singleton instance
    static JobSystem& Get();

    /// Initialize the job system with worker threads
    /// @param workerCount Number of worker threads (0 = auto-detect)
    void Initialize(uint32 workerCount = 0);

    /// Shutdown the job system, waits for all jobs to complete
    void Shutdown();

    /// Check if system is initialized
    bool IsInitialized() const { return m_Initialized.load(std::memory_order_acquire); }

    /// Schedule a job for execution
    void Schedule(const Job& job);

    /// Schedule multiple jobs
    void ScheduleBatch(const Job* jobs, usize count);

    /// Wait for a counter to reach zero (busy-wait with yielding)
    void WaitForCounter(JobCounter* counter);

    /// Get number of worker threads
    uint32 GetWorkerCount() const { return static_cast<uint32>(m_Workers.size()); }

    /// Get number of pending jobs in queue
    usize GetPendingJobCount() const { return m_JobQueue.GetSize(); }

    // Non-copyable singleton
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

private:
    JobSystem() = default;
    ~JobSystem();

    /// Worker thread entry point
    void WorkerLoop(uint32 workerId);

    /// Execute a single job
    void ExecuteJob(const Job& job);

    std::vector<std::thread> m_Workers;
    JobQueue m_JobQueue;
    std::condition_variable m_WakeCondition;
    std::mutex m_WakeMutex;
    std::atomic<bool> m_Initialized{false};
    std::atomic<bool> m_ShuttingDown{false};
};

} // namespace Dot
