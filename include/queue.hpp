// queue.hpp — queue contract + drivers (Laravel: Illuminate\Contracts\Queue).
//
// A Job is any callable. SyncQueue runs jobs immediately (Laravel's "sync" driver);
// ArrayQueue defers them until work() (handy for tests / batch processing). Header-only.
#pragma once
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

using Job = std::function<void()>;

class QueueContract {
public:
    virtual ~QueueContract() = default;
    virtual void push(Job job) = 0;
};

// Runs each job immediately on push.
class SyncQueue : public QueueContract {
public:
    void push(Job job) override { job(); }
};

// Stores jobs; work() runs and clears them.
class ArrayQueue : public QueueContract {
public:
    void push(Job job) override { jobs_.push_back(std::move(job)); }
    std::size_t size() const { return jobs_.size(); }
    void work() {
        for (auto& j : jobs_) j();
        jobs_.clear();
    }

private:
    std::vector<Job> jobs_;
};
