#pragma once

#include <future>
#include <vector>

namespace aura3d {

/// Guarantees every future in a vector is joined before the enclosing scope
/// unwinds.
///
/// Worker tasks capture stack data by reference, and `ink::ThreadPool`
/// futures -- unlike `std::async` ones -- do not wait on destruction. Without
/// this guard, a submit() that throws part-way leaves the already-running
/// tasks reading a frame that no longer exists.
class FutureJoiner {
public:
    /// Guards @p futures, which must outlive this object.
    explicit FutureJoiner(std::vector<std::future<void>>& futures) noexcept : _futures(futures) {}
    /// Waits for every valid future; errors are left in place for get().
    ~FutureJoiner() { for (auto& future : _futures) if (future.valid()) future.wait(); }
    FutureJoiner(const FutureJoiner&) = delete;
    FutureJoiner& operator=(const FutureJoiner&) = delete;

    /// Joins every future, then rethrows the first exception any of them
    /// raised. Later ones are dropped: all tasks have finished either way.
    void get()
    {
        std::exception_ptr error;
        for (auto& future : _futures)
        {
            if (!future.valid()) continue;
            try { future.get(); }
            catch (...) { if (!error) error = std::current_exception(); }
        }
        if (error) std::rethrow_exception(error);
    }

private:
    /// Borrowed: the caller may still be pushing into it while this guard lives.
    std::vector<std::future<void>>& _futures;
};

} // namespace aura3d
