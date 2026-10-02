#pragma once
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

class TVPStartOnceWorker {
    std::function<void()> work;
    std::mutex mutex;
    std::thread thread;

  public:
    explicit TVPStartOnceWorker(std::function<void()> task) : work(std::move(task)) {}
    ~TVPStartOnceWorker() {
        if (thread.joinable())
            thread.join();
    }
    void Start() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!thread.joinable())
            thread = std::thread(work);
    }
};
