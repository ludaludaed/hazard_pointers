#ifndef __ATOMIC_SHARED_POINTER_H__
#define __ATOMIC_SHARED_POINTER_H__

#include <lu/detail/atomic_ref_count_pointer.h>
#include <lu/hazard_pointer.h>
#include <lu/shared_ptr.h>


namespace lu {
namespace detail {

template <class ValueType>
struct SharedPointerTraits {
    using ref_count_ptr = shared_ptr<ValueType>;
    using control_block_ptr = typename ref_count_ptr::control_block_ptr;
    using guard = lu::hazard_pointer;

    static control_block_ptr get_control_block(ref_count_ptr &ptr) noexcept {
        return ptr.get_control_block();
    }

    static control_block_ptr release_pointer(ref_count_ptr &ptr) noexcept {
        return ptr.release();
    }

    static ref_count_ptr make_pointer(control_block_ptr control_block) noexcept {
        return ref_count_ptr(control_block);
    }

    static void dec_ref(control_block_ptr control_block) noexcept {
        control_block->dec_ref();
    }

    static void inc_ref(control_block_ptr control_block) noexcept {
        control_block->inc_ref();
    }

    static bool inc_ref_if_not_zero(control_block_ptr control_block) noexcept {
        return control_block->inc_ref_if_not_zero();
    }

    static guard make_guard() noexcept {
        return lu::make_hazard_pointer(get_ref_count_domain());
    }
};

}// namespace detail

template <class ValueType>
using atomic_shared_ptr = detail::AtomicRefCountPointer<detail::SharedPointerTraits<ValueType>>;

}// namespace lu

#endif
