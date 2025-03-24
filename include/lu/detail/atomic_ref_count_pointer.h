#ifndef __ATOMIC_REF_COUNT_POINTER_H__
#define __ATOMIC_REF_COUNT_POINTER_H__

#include <lu/hazard_pointer.h>

#include <atomic>


namespace lu {
namespace detail {

template<class RefCountTraits>
class AtomicRefCountPointer {
  using control_block_ptr = typename RefCountTraits::control_block_ptr;
  using ref_count_ptr = typename RefCountTraits::ref_count_ptr;

public:
  static constexpr bool is_always_lock_free = true;

private:
  static std::memory_order get_default_failure(std::memory_order success) {
    if (success == std::memory_order_acq_rel) {
      return std::memory_order_acquire;
    }
    if (success == std::memory_order_release) {
      return std::memory_order_relaxed;
    }
    return success;
  }

public:
  AtomicRefCountPointer() = default;

  AtomicRefCountPointer(const AtomicRefCountPointer &) = delete;

  AtomicRefCountPointer(AtomicRefCountPointer &&) = delete;

  ~AtomicRefCountPointer() {
    auto ptr = control_block_.load();
    if (ptr) {
      RefCountTraits::dec_ref(ptr);
    }
  }

  AtomicRefCountPointer &operator=(const AtomicRefCountPointer &) = delete;

  AtomicRefCountPointer &operator=(AtomicRefCountPointer &&) = delete;

  AtomicRefCountPointer &operator=(ref_count_ptr other) noexcept {
    store(std::move(other));
    return *this;
  }

  [[nodiscard]] bool is_lock_free() const noexcept {
    return true;
  }

  void store(ref_count_ptr desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
    auto desired_ptr = RefCountTraits::release_pointer(desired);
    auto old_ptr = control_block_.exchange(desired_ptr, order);
    if (old_ptr) {
      RefCountTraits::dec_ref(old_ptr);
    }
  }

  ref_count_ptr load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
    lu::hazard_pointer guard = lu::make_hazard_pointer();
    auto ptr = guard.protect(control_block_);
    while (ptr) {
      if (RefCountTraits::inc_ref_if_not_zero(ptr)) {
        return RefCountTraits::make_pointer(ptr);
      }
      ptr = guard.protect(control_block_);
    }
    return {};
  }

  ref_count_ptr exchange(ref_count_ptr desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
    auto desired_ptr = RefCountTraits::release_pointer(desired);
    auto old_ptr = control_block_.exchange(desired_ptr, order);
    return RefCountTraits::make_pointer(old_ptr);
  }

  bool compare_exchange_weak(ref_count_ptr &expected, ref_count_ptr desired, std::memory_order success,
                             std::memory_order failure) noexcept {
    auto expected_ptr = RefCountTraits::get_control_block(expected);
    auto desired_ptr = RefCountTraits::get_control_block(desired);
    if (control_block_.compare_exchange_weak(expected_ptr, desired_ptr, success, failure)) {
      if (expected_ptr) {
        RefCountTraits::dec_ref(expected_ptr);
      }
      RefCountTraits::release_pointer(desired);
      return true;
    } else {
      expected = load();
      return false;
    }
  }

  bool compare_exchange_strong(ref_count_ptr &expected, ref_count_ptr desired, std::memory_order success,
                               std::memory_order failure) noexcept {
    auto expected_ptr = RefCountTraits::get_control_block(expected);
    auto desired_ptr = RefCountTraits::get_control_block(desired);
    if (control_block_.compare_exchange_strong(expected_ptr, desired_ptr, success, failure)) {
      if (expected_ptr) {
        RefCountTraits::dec_ref(expected_ptr);
      }
      RefCountTraits::release_pointer(desired);
      return true;
    } else {
      expected = load();
      return false;
    }
  }

  bool compare_exchange_weak(ref_count_ptr &expected, ref_count_ptr desired,
                             std::memory_order success = std::memory_order_seq_cst) noexcept {
    return compare_exchange_weak(expected, desired, success, get_default_failure(success));
  }

  bool compare_exchange_strong(ref_count_ptr &expected, ref_count_ptr desired,
                               std::memory_order success = std::memory_order_seq_cst) noexcept {
    return compare_exchange_strong(expected, desired, success, get_default_failure(success));
  }

private:
  std::atomic<control_block_ptr> control_block_{};
};

}// namespace detail
}// namespace lu

#endif
