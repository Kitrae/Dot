// =============================================================================
// Dot Engine - Job
// =============================================================================
// Work unit for the job system. A job is a function + data that can be
// executed on any worker thread.
// =============================================================================

#pragma once

#include "Core/Core.h"

#include <atomic>
#include <functional>
#include <memory>

namespace Dot
{

/// Forward declarations
class JobSystem;

/// Job function signature
/// @param data User-provided data pointer
using JobFunction = void (*)(void* data);

// Cache line size for padding (typically 64 bytes on x86/x64)
constexpr usize kCacheLineSize = 64;

/// Atomic counter for job synchronization
/// Wait for counter to reach 0 to ensure all jobs complete.
/// Padded to prevent false sharing when multiple counters are adjacent.
class alignas(kCacheLineSize) DOT_CORE_API JobCounter
{
public:
    JobCounter() : m_Count(0) {}
    explicit JobCounter(int32 initial) : m_Count(initial) {}

    void Increment() { m_Count.fetch_add(1, std::memory_order_relaxed); }
    void Decrement() { m_Count.fetch_sub(1, std::memory_order_release); }

    int32 GetCount() const { return m_Count.load(std::memory_order_acquire); }
    bool IsComplete() const { return GetCount() <= 0; }

private:
    std::atomic<int32> m_Count;

    // Padding to fill cache line and prevent false sharing
    char m_Padding[kCacheLineSize - sizeof(std::atomic<int32>)];
};

/// Wrapper for heap-allocated lambda storage
/// Ensures proper cleanup when job completes
struct LambdaWrapper
{
    std::function<void()> func;

    static void Execute(void* data)
    {
        auto* wrapper = static_cast<LambdaWrapper*>(data);
        if (wrapper && wrapper->func)
        {
            wrapper->func();
        }
        delete wrapper; // Self-cleanup after execution
    }
};

/// Wrapper for frame-allocated lambda storage
/// NO cleanup needed - memory is freed when MemorySystem::EndFrame() is called
struct FrameLambdaWrapper
{
    std::function<void()> func;

    static void Execute(void* data)
    {
        auto* wrapper = static_cast<FrameLambdaWrapper*>(data);
        if (wrapper && wrapper->func)
        {
            wrapper->func();
        }
        // No delete - frame allocator memory is bulk-freed at EndFrame()
    }
};

/// Job declaration - describes work to be done
struct Job
{
    JobFunction function = nullptr; ///< Function to execute
    void* data = nullptr;           ///< User data passed to function
    JobCounter* counter = nullptr;  ///< Optional counter to decrement on completion

    /// Create a job from a function pointer (preferred for performance)
    static Job Create(JobFunction func, void* userData = nullptr, JobCounter* counter = nullptr)
    {
        Job job;
        job.function = func;
        job.data = userData;
        job.counter = counter;
        return job;
    }

    /// Create a job from a lambda (heap allocated - use sparingly)
    /// For high-frequency jobs, prefer Job::Create with a function pointer.
    ///
    /// WARNING: Each call allocates memory. For per-frame jobs, consider
    /// using CreateLambdaFrame() or function pointers with captured data.
    template <typename Func> static Job CreateLambda(Func&& func, JobCounter* counter = nullptr)
    {
        // Heap allocate the lambda - will be deleted after execution
        auto* wrapper = new LambdaWrapper();
        wrapper->func = std::forward<Func>(func);

        Job job;
        job.function = &LambdaWrapper::Execute;
        job.data = wrapper;
        job.counter = counter;
        return job;
    }

    /// Create a job from a lambda using the frame allocator (zero heap allocation)
    ///
    /// CRITICAL: The job MUST complete before MemorySystem::EndFrame() is called,
    /// otherwise the lambda data will be invalid and cause undefined behavior.
    /// Use this for jobs you schedule and immediately wait on with WaitForCounter().
    ///
    /// Usage:
    /// @code
    ///     JobCounter counter(1);
    ///     JobSystem::Get().Schedule(Job::CreateLambdaFrame([&]() { /* work */ }, &counter));
    ///     JobSystem::Get().WaitForCounter(&counter); // Must wait before EndFrame!
    /// @endcode
    template <typename Func> static Job CreateLambdaFrame(Func&& func, JobCounter* counter = nullptr)
    {
        // Use frame allocator - no cleanup needed, memory freed at EndFrame()
        auto* wrapper = MemorySystem::Get().FrameNew<FrameLambdaWrapper>();
        if (!wrapper)
        {
            // Fall back to heap if frame allocator is full or not in frame
            return CreateLambda(std::forward<Func>(func), counter);
        }
        wrapper->func = std::forward<Func>(func);

        Job job;
        job.function = &FrameLambdaWrapper::Execute;
        job.data = wrapper;
        job.counter = counter;
        return job;
    }
};

} // namespace Dot
