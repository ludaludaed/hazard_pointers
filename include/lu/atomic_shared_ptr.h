#ifndef __ATOMIC_SHARED_POINTER_H__
#define __ATOMIC_SHARED_POINTER_H__

#include <lu/detail/atomic_ref_count_pointer.h>
#include <lu/shared_ptr.h>


namespace lu {
namespace detail {

template<class ValueType>
struct SharedPointerTraits {
  using ref_count_ptr = shared_ptr<ValueType>;
  using control_block_ptr = typename ref_count_ptr::control_block_ptr;

  static control_block_ptr get_control_block(ref_count_ptr &ptr) {
    return ptr.GetControlBlock();
  }

  static control_block_ptr release_pointer(ref_count_ptr &ptr) {
    return ptr.Release();
  }

  static ref_count_ptr make_pointer(control_block_ptr control_block) {
    return ref_count_ptr(control_block);
  }

  static void dec_ref(control_block_ptr control_block) {
    control_block->DecRef();
  }

  static void inc_ref(control_block_ptr control_block) {
    control_block->IncRef();
  }

  static bool inc_ref_if_not_zero(control_block_ptr control_block) {
    return control_block->IncRefIfNotZero();
  }
};

}// namespace detail

template<class ValueType>
using atomic_shared_ptr = detail::AtomicRefCountPointer<detail::SharedPointerTraits<ValueType>>;

}// namespace lu

#endif
