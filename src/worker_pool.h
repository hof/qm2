#ifndef MAXIMA_WORKER_POOL_H
#define MAXIMA_WORKER_POOL_H

#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

/* The owner serializes dispatch/resize. Jobs own their search state; pool locks
 * cover job handoff only, never search work. Resizing first waits for every job.
 * A main search job may wait for helper slots, never for its own slot. */
class worker_pool_t {
    struct worker_t {
        std::mutex mutex;
        std::condition_variable changed;
        std::function<void()> job;
        std::exception_ptr failure;
        bool busy = false;
        bool exiting = false;
        std::thread thread;

        worker_t() : thread([this] { loop(); }) {}
        ~worker_t() {
            {
                std::lock_guard lock(mutex);
                exiting = true;
            }
            changed.notify_one();
            thread.join();
        }

        void loop() {
            std::unique_lock lock(mutex);
            for (;;) {
                changed.wait(lock, [this] { return busy || exiting; });
                if (!busy && exiting) {
                    return;
                }
                auto task = std::move(job);
                lock.unlock();
                std::exception_ptr error;
                try {
                    task();
                } catch (...) {
                    error = std::current_exception();
                }
                // Release captures before completion permits workspace replacement.
                task = {};
                lock.lock();
                failure = error;
                busy = false;
                changed.notify_all();
            }
        }

        std::exception_ptr wait() {
            std::unique_lock lock(mutex);
            changed.wait(lock, [this] { return !busy; });
            auto error = failure;
            failure = nullptr;
            return error;
        }
    };

    std::vector<std::unique_ptr<worker_t>> workers;

public:
    static constexpr size_t max_workers = 256;
    ~worker_pool_t() {
        try {
            wait();
        } catch (...) {
            // All jobs have finished; destructors must still join the workers.
        }
    }
    size_t size() const { return workers.size(); }

    void wait() {
        std::exception_ptr error;
        for (auto & worker : workers) {
            const auto next = worker->wait();
            if (!error) {
                error = next;
            }
        }
        if (error) {
            std::rethrow_exception(error);
        }
    }

    void wait_for(size_t index) {
        if (index >= workers.size()) {
            throw std::out_of_range("invalid search worker index");
        }
        if (auto error = workers[index]->wait()) {
            std::rethrow_exception(error);
        }
    }

    void resize(size_t count) {
        if (count == 0 || count > max_workers) {
            throw std::invalid_argument("invalid search worker count");
        }
        wait();
        // Stage additions before changing the live set. If allocation or thread
        // creation fails, staged idle workers shut down and the old set survives.
        workers.reserve(count);
        std::vector<std::unique_ptr<worker_t>> additions;
        for (size_t i = workers.size(); i < count; ++i) {
            additions.push_back(std::make_unique<worker_t>());
        }
        for (auto & worker : additions) {
            workers.push_back(std::move(worker));
        }
        workers.resize(count);
    }

    void run(size_t index, std::function<void()> job) {
        if (index >= workers.size() || !job) {
            throw std::invalid_argument("invalid search worker job");
        }
        auto & worker = *workers[index];
        {
            std::lock_guard lock(worker.mutex);
            if (worker.busy || worker.failure) {
                throw std::logic_error("search worker has not been joined");
            }
            worker.job = std::move(job);
            worker.busy = true;
        }
        worker.changed.notify_one();
    }
};

#endif
