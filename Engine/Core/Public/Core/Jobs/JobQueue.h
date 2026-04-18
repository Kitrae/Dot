// =============================================================================
// Dot Engine - Job Queue
// =============================================================================
// Thread-safe queue for jobs. Uses mutex for simplicity.
// =============================================================================

#pragma once

#include "Core/Jobs/Job.h"

#include <deque>
#include <mutex>
#include <optional>


namespace Dot
{

/// Thread-safe job queue
class DOT_CORE_API JobQueue
{
public:
    JobQueue() = default;
    ~JobQueue() = default;

    // Non-copyable
    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    /// Push a job to the back of the queue
    void Push(const Job& job)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Jobs.push_back(job);
    }

    /// Try to pop a job from the front of the queue
    /// @return Job if available, nullopt otherwise
    std::optional<Job> TryPop()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Jobs.empty())
        {
            return std::nullopt;
        }

        Job job = m_Jobs.front();
        m_Jobs.pop_front();
        return job;
    }

    /// Check if queue is empty
    bool IsEmpty() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Jobs.empty();
    }

    /// Get number of jobs in queue
    usize GetSize() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Jobs.size();
    }

    /// Clear all jobs
    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Jobs.clear();
    }

private:
    mutable std::mutex m_Mutex;
    std::deque<Job> m_Jobs;
};

} // namespace Dot
