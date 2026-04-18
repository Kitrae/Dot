// =============================================================================
// Dot Engine - Job System Unit Tests
// =============================================================================

#include "Core/Jobs/JobSystem.h"

#include <atomic>
#include <gtest/gtest.h>

using namespace Dot;

// =============================================================================
// JobCounter Tests
// =============================================================================

TEST(JobCounterTests, InitialValue)
{
    JobCounter counter;
    EXPECT_EQ(counter.GetCount(), 0);
    EXPECT_TRUE(counter.IsComplete());
}

TEST(JobCounterTests, InitialValueNonZero)
{
    JobCounter counter(5);
    EXPECT_EQ(counter.GetCount(), 5);
    EXPECT_FALSE(counter.IsComplete());
}

TEST(JobCounterTests, IncrementDecrement)
{
    JobCounter counter;

    counter.Increment();
    EXPECT_EQ(counter.GetCount(), 1);

    counter.Increment();
    EXPECT_EQ(counter.GetCount(), 2);

    counter.Decrement();
    EXPECT_EQ(counter.GetCount(), 1);

    counter.Decrement();
    EXPECT_EQ(counter.GetCount(), 0);
    EXPECT_TRUE(counter.IsComplete());
}

// =============================================================================
// JobQueue Tests
// =============================================================================

TEST(JobQueueTests, PushPop)
{
    JobQueue queue;

    int value = 42;
    Job job = Job::Create([](void* data) { (void)data; }, &value);

    queue.Push(job);
    EXPECT_FALSE(queue.IsEmpty());
    EXPECT_EQ(queue.GetSize(), 1);

    auto popped = queue.TryPop();
    EXPECT_TRUE(popped.has_value());
    EXPECT_EQ(popped->data, &value);
    EXPECT_TRUE(queue.IsEmpty());
}

TEST(JobQueueTests, EmptyPop)
{
    JobQueue queue;

    auto popped = queue.TryPop();
    EXPECT_FALSE(popped.has_value());
}

TEST(JobQueueTests, FIFO)
{
    JobQueue queue;

    int v1 = 1, v2 = 2, v3 = 3;
    queue.Push(Job::Create([](void* d) { (void)d; }, &v1));
    queue.Push(Job::Create([](void* d) { (void)d; }, &v2));
    queue.Push(Job::Create([](void* d) { (void)d; }, &v3));

    EXPECT_EQ(queue.TryPop()->data, &v1);
    EXPECT_EQ(queue.TryPop()->data, &v2);
    EXPECT_EQ(queue.TryPop()->data, &v3);
    EXPECT_FALSE(queue.TryPop().has_value());
}

// =============================================================================
// JobSystem Tests
// =============================================================================

class JobSystemTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Initialize with 2 workers for testing
        JobSystem::Get().Initialize(2);
    }

    void TearDown() override { JobSystem::Get().Shutdown(); }
};

TEST_F(JobSystemTests, Initialization)
{
    EXPECT_TRUE(JobSystem::Get().IsInitialized());
    EXPECT_GE(JobSystem::Get().GetWorkerCount(), 1);
}

TEST_F(JobSystemTests, SimpleJob)
{
    std::atomic<bool> executed{false};
    JobCounter counter(1);

    JobSystem::Get().Schedule(Job::Create(
        [](void* data)
        {
            auto* flag = static_cast<std::atomic<bool>*>(data);
            flag->store(true);
        },
        &executed, &counter));

    JobSystem::Get().WaitForCounter(&counter);

    EXPECT_TRUE(executed.load());
}

TEST_F(JobSystemTests, MultipleJobs)
{
    constexpr int kJobCount = 10;
    std::atomic<int> counter{0};
    JobCounter jobCounter(kJobCount);

    for (int i = 0; i < kJobCount; ++i)
    {
        JobSystem::Get().Schedule(Job::Create(
            [](void* data)
            {
                auto* cnt = static_cast<std::atomic<int>*>(data);
                cnt->fetch_add(1);
            },
            &counter, &jobCounter));
    }

    JobSystem::Get().WaitForCounter(&jobCounter);

    EXPECT_EQ(counter.load(), kJobCount);
}

TEST_F(JobSystemTests, ParallelExecution)
{
    constexpr int kJobCount = 100;
    std::atomic<int> counter{0};
    JobCounter jobCounter(kJobCount);

    for (int i = 0; i < kJobCount; ++i)
    {
        JobSystem::Get().Schedule(Job::Create(
            [](void* data)
            {
                auto* cnt = static_cast<std::atomic<int>*>(data);
                cnt->fetch_add(1);
            },
            &counter, &jobCounter));
    }

    JobSystem::Get().WaitForCounter(&jobCounter);

    // All jobs should have completed
    EXPECT_EQ(counter.load(), kJobCount);
}
