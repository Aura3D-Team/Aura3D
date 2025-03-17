#include "AuraThreadPool.h"

namespace aura3d {

AuraThreadPool::AuraThreadPool(size_t max_workers) :
    _stop(false), _activeWorkers(0)
{
    for (size_t i = 0; i < max_workers; ++i)
    {
        _workers.emplace_back([this] {
            while (true)
            {
                std::function<void()> _task;
                {
                    std::unique_lock<std::mutex> lock(_queueMutex);
                    _condition.wait(lock, [this]{ return _stop || !_tasks.empty(); });

                    if (_stop && _tasks.empty()) return;

                    _task = std::move(_tasks.front());
                    _tasks.pop();
                    _activeWorkers++;
                }

                _task();

                _activeWorkers--;
                _condition.notify_all();
            }
        });
    }
}

AuraThreadPool::~AuraThreadPool() {
    {
        std::unique_lock<std::mutex> lock(_queueMutex);
        _stop = true;
    }
    _condition.notify_all();
    for (std::thread& worker : _workers) {
        worker.join();
    }
}

void AuraThreadPool::wait() {
    std::unique_lock<std::mutex> lock(_queueMutex);
    _condition.wait(lock, [this] { return _tasks.empty() && (_activeWorkers == 0); });
}

}
