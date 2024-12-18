#ifndef __MARKED_SHARED_PTR_H__
#define __MARKED_SHARED_PTR_H__

#include "marked_ptr.h"
#include "shared_ptr.h"

#include <memory>
#include <type_traits>
#include <utility>


namespace lu {
    template<class ValueType>
    class MarkedSharedPointer : public StrongRefCountPointer<ValueType *, MarkedPointer<ControlBlock>> {
        using Base = StrongRefCountPointer<ValueType *, MarkedPointer<ControlBlock>>;

        template<class>
        friend class MarkedSharedPointer;

        template<class>
        friend class MarkedSharedPointerTraits;

    public:
        using Base::Base;

        using element_type = typename Base::element_type;
        using control_block_type = typename Base::control_block_type;

        using control_block_ptr = typename Base::control_block_ptr;
        using element_ptr = typename Base::element_ptr;

    private:
        explicit MarkedSharedPointer(control_block_ptr control_block) {
            this->SetData(reinterpret_cast<element_ptr>(control_block->get()), control_block);
        }

    public:
        MarkedSharedPointer() noexcept = default;

        template<class _ValueType,
                 class Deleter = std::default_delete<_ValueType>,
                 class Allocator = std::allocator<_ValueType>,
                 class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
        explicit MarkedSharedPointer(_ValueType *value_ptr, Deleter deleter = {}, const Allocator &allocator = {}) noexcept {
            Construct(value_ptr, std::move(deleter), allocator);
        }

        MarkedSharedPointer(const MarkedSharedPointer &other) noexcept {
            this->CopyConstruct(other);
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
        MarkedSharedPointer(const StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
            this->CopyConstruct(other);
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
        MarkedSharedPointer(const StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &other, bool bit_value) noexcept {
            this->CopyConstruct(other);
            this->control_block_ = control_block_ptr(this->control_block_.get(), bit_value);
        }

        MarkedSharedPointer(MarkedSharedPointer &&other) noexcept {
            this->MoveConstruct(std::move(other));
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
        MarkedSharedPointer(StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
            this->MoveConstruct(std::move(other));
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
        explicit MarkedSharedPointer(const WeakRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) {
            this->ConstructFromWeak(other);
        }

        ~MarkedSharedPointer() {
            this->DecRef();
        }

        MarkedSharedPointer &operator=(const MarkedSharedPointer &other) noexcept {
            MarkedSharedPointer temp(other);
            this->swap(temp);
            return *this;
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
        MarkedSharedPointer &operator=(const StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
            MarkedSharedPointer temp(other);
            this->swap(temp);
            return *this;
        }

        MarkedSharedPointer &operator=(MarkedSharedPointer &&other) noexcept {
            MarkedSharedPointer temp(std::move(other));
            this->swap(temp);
            return *this;
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
        MarkedSharedPointer &operator=(StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
            MarkedSharedPointer temp(std::move(other));
            this->swap(temp);
            return *this;
        }

        template<class _ValueType,
                 class Deleter = std::default_delete<_ValueType>,
                 class Allocator = std::allocator<_ValueType>,
                 class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
        void reset(_ValueType *value_ptr = {}, Deleter deleter = {}, const Allocator &allocator = {}) noexcept {
            MarkedSharedPointer temp(value_ptr, std::move(deleter), allocator);
            this->swap(temp);
        }

        void set_bit() {
            this->control_block_.set_bit();
        }

        void clear_bit() {
            this->control_block_.clear_bit();
        }

        bool get_bit() {
            return this->control_block_.get_bit();
        }

        friend bool operator==(const MarkedSharedPointer &left, const MarkedSharedPointer &right) noexcept {
            return left.get() == right.get() && left.control_block_ == right.control_block_;
        }

        friend bool operator!=(const MarkedSharedPointer &left, const MarkedSharedPointer &right) noexcept {
            return !(left == right);
        }

    private:
        template<class _ValueType,
                 class Deleter = std::default_delete<_ValueType>,
                 class Allocator = std::allocator<_ValueType>>
        void Construct(_ValueType *value_ptr, Deleter deleter = {}, const Allocator &allocator = {}) noexcept {
            auto control_block = make_outplace_control_block<_ValueType>(value_ptr, std::move(deleter), allocator);
            this->SetData(value_ptr, control_block);
        }
    };

    template<class ValueType>
    struct MarkedSharedPointerTraits {
        using ref_count_ptr = MarkedSharedPointer<ValueType>;
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
            control_block->dec_ref();
        }

        static void inc_ref(control_block_ptr control_block) {
            control_block->inc_ref();
        }

        static bool inc_ref_if_not_zero(control_block_ptr control_block) {
            return control_block->inc_ref_if_not_zero();
        }
    };

    template<class ValueType>
    using marked_shared_ptr = MarkedSharedPointer<ValueType>;

    template<class ValueType>
    using atomic_marked_shared_ptr = AtomicRefCountPointer<MarkedSharedPointerTraits<ValueType>>;
}// namespace lu

#endif