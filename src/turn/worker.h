#pragma once
#include "localhistoria/world.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <trantor/utils/Logger.h>
namespace lh {
class Worker {
    std::mutex m;
    std::condition_variable cv;
    std::queue<std::function<void()>> tasks;
    bool stop = false;
    bool drain;
    std::thread thread;

  public:
    explicit Worker(bool drain = true)
        : drain(drain), thread([this] {
              for (;;) {
                  std::function<void()> f;
                  {
                      std::unique_lock l(m);
                      cv.wait(l, [&] { return stop || !tasks.empty(); });
                      if (stop && tasks.empty())
                          return;
                      f = std::move(tasks.front());
                      tasks.pop();
                  }
                  try {
                      f();
                  } catch (const std::exception &e) {
                      LOG_ERROR << e.what();
                  }
              }
          }) {}
    ~Worker() { finish(); }
    void finish() {
        {
            std::lock_guard l(m);
            stop = true;
            if (!drain)
                tasks = {};
        }
        cv.notify_all();
        if (thread.joinable())
            thread.join();
    }
    void add(std::function<void()> f) {
        std::lock_guard l(m);
        if (stop || tasks.size() >= 8)
            throw ValidationError("Work queue full; retry later");
        tasks.push(std::move(f));
        cv.notify_one();
    }
};
} // namespace lh
