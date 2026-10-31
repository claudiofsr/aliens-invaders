#ifndef RECYCLE_BIN_H
#define RECYCLE_BIN_H

#include <cstddef>
#include <memory>
#include <vector>

/** @class RecycleBin @brief Pure LIFO free-list of owned heap objects (AGENT 4 / AGENT 7). Why
 * (anti-stuttering): - Every malloc/free pair inside the 60 Hz loop is a potential frame ... */
template <typename T>
class RecycleBin {
  std::vector<std::unique_ptr<T>> bin_;

 public:
  RecycleBin() = default;

  // The bin owns unique handles: copying is meaningless; moving keeps the
  // ownership story explicit.
  RecycleBin(const RecycleBin&) = delete;
  RecycleBin& operator=(const RecycleBin&) = delete;
  RecycleBin(RecycleBin&&) noexcept = default;
  RecycleBin& operator=(RecycleBin&&) noexcept = default;

  /// One-shot capacity sizing at startup. After this call (with a correct bound) Release() can
  /// never grow the vector -- that is what makes the noexcept contract below honest.
  void Reserve(std::size_t capacity) { bin_.reserve(capacity); }

  [[nodiscard]] std::size_t Size() const noexcept { return bin_.size(); }
  [[nodiscard]] bool Empty() const noexcept { return bin_.empty(); }

  /// Returns an object to the pool instead of freeing its memory.
  /// Releasing a null handle is a safe no-op.
  void Release(std::unique_ptr<T> object) noexcept {
    if (object) bin_.push_back(std::move(object));
  }

  /// LIFO acquire: the warmest (most recently released) object, or nullptr
  /// when the pool is cold and the caller must allocate a fresh one.
  [[nodiscard]] std::unique_ptr<T> TryAcquire() noexcept {
    if (bin_.empty()) return nullptr;
    std::unique_ptr<T> object = std::move(bin_.back());
    bin_.pop_back();
    return object;
  }
};

#endif  // RECYCLE_BIN_H
