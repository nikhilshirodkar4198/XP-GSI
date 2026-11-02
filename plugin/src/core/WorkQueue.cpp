#include "WorkQueue.h"
#include "Log.h"
#include "core/Cancel.h"

WorkQueue::WorkQueue() {
    mThread = std::thread(&WorkQueue::workerLoop, this);
}

WorkQueue::~WorkQueue() {
    shutdown();
}

void WorkQueue::post(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(mTaskMux);
        if (mStop) return;
        mTasks.push(std::move(fn));
    }
    mTaskCV.notify_one();
}

void WorkQueue::postToMain(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(mMainMux);
    mMainCBs.push(std::move(fn));
}

void WorkQueue::drainMain() {
    // Swap under lock to minimise hold time; run callbacks lock-free.
    std::queue<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lk(mMainMux);
        std::swap(local, mMainCBs);
    }
    while (!local.empty()) {
        local.front()();
        local.pop();
    }
}

void WorkQueue::shutdown() {
    Cancel::flag() = true;
    {
        std::lock_guard<std::mutex> lk(mTaskMux);
        mStop = true;
        // Queued jobs are dropped: X-Plane is quitting or reloading plugins,
        // nothing would use their results.
        std::queue<std::function<void()>>().swap(mTasks);
    }
    mTaskCV.notify_one();
    if (mThread.joinable()) mThread.join();
}

void WorkQueue::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(mTaskMux);
            mTaskCV.wait(lk, [this] { return mStop || !mTasks.empty(); });
            if (mStop && mTasks.empty()) break;
            task = std::move(mTasks.front());
            mTasks.pop();
        }
        try {
            task();
        } catch (const std::exception& e) {
            Log::error("WorkQueue: uncaught exception: %s", e.what());
        } catch (...) {
            Log::error("WorkQueue: uncaught unknown exception");
        }
    }
}
