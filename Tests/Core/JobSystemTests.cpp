#include "Core/Jobs/JobSystem.h"

#include <atomic>
#include <gtest/gtest.h>

using namespace Dot;

class JobSystemTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!JobSystem::Get().IsInitialized())
        {
            JobSystem::Get().Initialize(4);
        }
    }

    void TearDown() override
    {
        // Keep it initialized for other tests if needed, or shut down
        // JobSystem::Get().Shutdown();
    }
};

TEST_F(JobSystemTests, BasicJob)
{
    static std::atomic<int> callCount{0};
    callCount = 0;

    auto work = [](void*) { callCount.fetch_add(1); };

    JobCounter counter;
    counter.Increment();

    JobSystem::Get().Schedule(Job::Create(work, nullptr, &counter));

    JobSystem::Get().WaitForCounter(&counter);

    EXPECT_EQ(callCount.load(), 1);
}

TEST_F(JobSystemTests, LambdaJob)
{
    std::atomic<int> value{0};
    JobCounter counter;
    counter.Increment();

    auto job = Job::CreateLambda([&value]() { value.store(42); }, &counter);

    JobSystem::Get().Schedule(job);
    JobSystem::Get().WaitForCounter(&counter);

    EXPECT_EQ(value.load(), 42);
}

TEST_F(JobSystemTests, BatchJobs)
{
    constexpr int kJobCount = 100;
    std::atomic<int> sum{0};
    JobCounter counter(kJobCount);

    auto work = [](void* data)
    {
        auto* s = static_cast<std::atomic<int>*>(data);
        s->fetch_add(1);
    };

    std::vector<Job> jobs;
    for (int i = 0; i < kJobCount; ++i)
    {
        jobs.push_back(Job::Create(work, &sum, &counter));
    }

    JobSystem::Get().ScheduleBatch(jobs.data(), jobs.size());
    JobSystem::Get().WaitForCounter(&counter);

    EXPECT_EQ(sum.load(), kJobCount);
}

TEST_F(JobSystemTests, NestedJobs)
{
    std::atomic<int> result{0};
    JobCounter outerCounter(1);

    auto innerWork = [](void* data)
    {
        auto* r = static_cast<std::atomic<int>*>(data);
        r->fetch_add(1);
    };

    auto outerWork = [innerWork, &result]()
    {
        JobCounter innerCounter(1);
        JobSystem::Get().Schedule(Job::Create(innerWork, &result, &innerCounter));
        JobSystem::Get().WaitForCounter(&innerCounter);
    };

    JobSystem::Get().Schedule(Job::CreateLambda(outerWork, &outerCounter));
    JobSystem::Get().WaitForCounter(&outerCounter);

    EXPECT_EQ(result.load(), 1);
}
