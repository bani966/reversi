#pragma once

#include <atomic>

namespace reversi {

// A minimal, portable cooperative-cancellation flag. Deliberately not std::stop_token: that
// only left -fexperimental-library in libc++ 20 (~early 2025), and Apple's shipped libc++ has
// historically lagged upstream on exactly this feature, which is too much portability risk
// for engine/'s public API. Shared (e.g. via std::shared_ptr) between the thread requesting
// cancellation and the thread running search(); safe to poll from any thread.
class CancellationToken {
public:
    void requestStop() { stopped_.store(true, std::memory_order_relaxed); }
    bool stopRequested() const { return stopped_.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> stopped_{false};
};

} // namespace reversi
