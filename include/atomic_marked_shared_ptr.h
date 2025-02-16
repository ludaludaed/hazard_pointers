#ifndef __ATOMIC_MARKED_SHARED_POINTER_H__
#define __ATOMIC_MARKED_SHARED_POINTER_H__

#include <detail/atomic_ref_count_pointer.h>
#include <marked_shared_ptr.h>

namespace lu {
namespace detail {

template<class ValueType>
struct MarkedSharedPointerTraits {
    using ref_count_ptr = marked_shared_ptr<ValueType>;
    using control_block_ptr = typename ref_count_ptr::control_block_ptr;

    static control_block_ptr get_control_block(ref_count_ptr &ptr) {
        return ptr.GetControlBlock();
    }

    static control_block_ptr release_ptr(ref_count_ptr &ptr) {
        return ptr.Release();
    }

    static ref_count_ptr create_ptr(control_block_ptr control_block) {
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
using atomic_marked_shared_ptr = detail::AtomicRefCountPointer<detail::MarkedSharedPointerTraits<ValueType>>;

}// namespace lu

#endif
