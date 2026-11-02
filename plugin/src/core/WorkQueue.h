#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

// Single-worker thread queue with a separate main-thread delivery queue.
//
// Rule: XPLM functions must never be called from the worker thread.
// Worker tasks use postToMain() to hand results back; drainMain() is called
// from the flight loop (main thread only) to execute those callbacks.
class WorkQueue {
public:
    WorkQueue();
    ~WorkQueue(); // calls shutdown()

    // Enqueue work on the worker thread. Thread-safe.
    void post(std::function<void()> fn);

    // Enqueue a callback to run on the main thread. Thread-safe.
    // Delivered when drainMain() is next called.
    void postToMain(std::function<void()> fn);

    // Execute all pending main-thread callbacks. Call from the flight loop only.
    void drainMain();

    // Signal stop (Cancel::flag, so a running job ends early), drop the
    // pending tasks and join the worker thread. After shutdown(), post() is
    // silently ignored.
    void shutdown();

private:
    void workerLoop();

    std::thread                        mThread;
    std::queue<std::function<void()>>  mTasks;
    std::mutex                         mTaskMux;
    std::condition_variable            mTaskCV;
    bool                               mStop  = false;

    std::queue<std::function<void()>>  mMainCBs;
    std::mutex                         mMainMux;
};
