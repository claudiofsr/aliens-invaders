#ifndef CORE_THREADING_H
#define CORE_THREADING_H

// One spelling of cooperative threading (AGENT 6): std::jthread semantics on
// every platform -- std types where P0660 ships, equivalent fallback where not.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__has_include) && __has_include(<stop_token>)
#include <stop_token>
#endif

// The hitch recorder hands frames over through std::atomic<T>::wait/notify;
// every supported toolchain (GCC 11+, LLVM/AppleClang 14+, MSVC 19.28+) has it.
#if !defined(__cpp_lib_atomic_wait)
#error "threading.h requires C++20 std::atomic::wait -- the toolchain is too old."
#endif

// Define ALIENS_THREADING_FORCE_FALLBACK to exercise the fallback on any
// platform (debugging aid; the standard path is always preferred otherwise).
#if defined(__cpp_lib_jthread) && !defined(ALIENS_THREADING_FORCE_FALLBACK)
#define ALIENS_THREADING_STD 1
#endif

namespace Portable {

#if defined(ALIENS_THREADING_STD)

using StopToken = std::stop_token;
using StopSource = std::stop_source;
using JThread = std::jthread;
template <typename Callback>
using StopCallback = std::stop_callback<Callback>;

#else

namespace detail {

// Shared cancel state: one atomic flag plus a callback registry. The flag is
// atomic so stop_requested() never locks (std semantics on the polling path).
class StopState {
 public:
  [[nodiscard]] bool StopRequested() const noexcept {
    return requested_.load(std::memory_order_acquire);
  }

  // Flips the flag once and runs the registered callbacks under the registry
  // lock, so ~StopCallback (Unregister) can never race a running callback.
  bool RequestStop() {
    if (requested_.exchange(true, std::memory_order_acq_rel)) return false;
    const std::lock_guard<std::mutex> lock(mu_);
    for (const Entry& entry : callbacks_) {
      if (entry.fn) entry.fn();
    }
    return true;
  }

  // Registry id (> 0) on success; 0 when the stop already happened, in which
  // case the callback fires inline -- the std::stop_callback construction rule.
  std::size_t Register(const std::function<void()>& fn) {
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (!requested_.load(std::memory_order_acquire)) {
        callbacks_.push_back({++next_id_, fn});
        return next_id_;
      }
    }
    fn();
    return 0;
  }

  void Unregister(std::size_t id) {
    if (id == 0) return;
    const std::lock_guard<std::mutex> lock(mu_);
    for (auto it = callbacks_.begin(); it != callbacks_.end(); ++it) {
      if (it->id == id) {
        callbacks_.erase(it);
        return;
      }
    }
  }

 private:
  struct Entry {
    std::size_t id;
    std::function<void()> fn;
  };
  mutable std::mutex mu_;
  std::atomic<bool> requested_{false};
  std::size_t next_id_{0};
  std::vector<Entry> callbacks_;
};

}  // namespace detail

class StopToken {
 public:
  StopToken() noexcept = default;
  explicit StopToken(std::shared_ptr<detail::StopState> state) noexcept
      : state_(std::move(state)) {}

  [[nodiscard]] bool stop_requested() const noexcept {
    return state_ && state_->StopRequested();
  }
  [[nodiscard]] bool stop_possible() const noexcept {
    return static_cast<bool>(state_);
  }

 private:
  template <typename Callback>
  friend class StopCallback;
  friend class JThread;
  [[nodiscard]] const std::shared_ptr<detail::StopState>& State() const noexcept {
    return state_;
  }

  std::shared_ptr<detail::StopState> state_;
};

template <typename Callback>
class StopCallback {
 public:
  StopCallback(const StopToken& token, Callback callback)
      : state_(token.State()), callback_(std::move(callback)) {
    if (state_) id_ = state_->Register(callback_);
  }
  ~StopCallback() {
    if (state_) state_->Unregister(id_);
  }
  StopCallback(const StopCallback&) = delete;
  StopCallback& operator=(const StopCallback&) = delete;

 private:
  std::shared_ptr<detail::StopState> state_;
  Callback callback_;
  std::size_t id_{0};
};
template <typename Callback>
StopCallback(const StopToken&, Callback) -> StopCallback<Callback>;

// RAII owner of std::thread + StopState: the destructor requests stop and
// joins, exactly like std::jthread; a move-assign joins the target first.
class JThread {
 public:
  JThread() noexcept = default;
  template <typename Fn, typename... Args>
  explicit JThread(Fn&& fn, Args&&... args)
      : state_(std::make_shared<detail::StopState>()) {
    if constexpr (std::is_invocable_v<std::decay_t<Fn>, StopToken,
                                      std::decay_t<Args>...>) {
      thread_ = std::thread(std::forward<Fn>(fn), StopToken(state_),
                            std::forward<Args>(args)...);
    } else {
      thread_ = std::thread(std::forward<Fn>(fn), std::forward<Args>(args)...);
    }
  }
  ~JThread() {
    if (thread_.joinable()) {
      if (state_) state_->RequestStop();
      thread_.join();
    }
  }
  JThread(const JThread&) = delete;
  JThread& operator=(const JThread&) = delete;
  JThread(JThread&& other) noexcept
      : state_(std::move(other.state_)), thread_(std::move(other.thread_)) {}
  JThread& operator=(JThread&& other) noexcept {
    if (this != &other) {
      if (thread_.joinable()) {
        if (state_) state_->RequestStop();
        thread_.join();
      }
      state_ = std::move(other.state_);
      thread_ = std::move(other.thread_);
    }
    return *this;
  }

  bool request_stop() noexcept { return state_ && state_->RequestStop(); }
  [[nodiscard]] StopToken get_stop_token() const noexcept {
    return StopToken(state_);
  }
  [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }
  void join() { thread_.join(); }
  [[nodiscard]] std::thread::id get_id() const noexcept {
    return thread_.get_id();
  }

 private:
  std::shared_ptr<detail::StopState> state_;
  std::thread thread_;
};

#endif  // ALIENS_THREADING_STD

// One cancel-aware wait spelling for both condition_variable flavours: the std
// P0660 overload where available, a stop-callback wakeup otherwise.
#if defined(ALIENS_THREADING_STD)
template <typename Cv, typename Lock, typename Pred>
void CvWaitStop(Cv& cv, Lock& lock, const StopToken& st, Pred pred) {
  (void)cv.wait(lock, st, std::move(pred));
}
#else
template <typename Cv, typename Lock, typename Pred>
void CvWaitStop(Cv& cv, Lock& lock, const StopToken& st, Pred pred) {
  const StopCallback<std::function<void()>> wake(st,
                                                 [&cv]() { cv.notify_all(); });
  while (!pred()) cv.wait(lock);
}
#endif

// Timed variant of the cancel-aware wait (telemetry's bounded 500 ms poll):
// one spelling on both paths, same steady-clock semantics as the std overload.
#if defined(ALIENS_THREADING_STD)
template <typename Cv, typename Lock, typename Rep, typename Period,
          typename Pred>
void CvWaitForStop(Cv& cv, Lock& lock, const StopToken& st,
                   const std::chrono::duration<Rep, Period>& timeout,
                   Pred pred) {
  (void)cv.wait_for(lock, st, timeout, std::move(pred));
}
#else
template <typename Cv, typename Lock, typename Rep, typename Period,
          typename Pred>
void CvWaitForStop(Cv& cv, Lock& lock, const StopToken& st,
                   const std::chrono::duration<Rep, Period>& timeout,
                   Pred pred) {
  const StopCallback<std::function<void()>> wake(st,
                                                 [&cv]() { cv.notify_all(); });
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred() && !st.stop_requested()) {
    if (cv.wait_until(lock, deadline) == std::cv_status::timeout) break;
  }
}
#endif

}  // namespace Portable

#endif  // CORE_THREADING_H
