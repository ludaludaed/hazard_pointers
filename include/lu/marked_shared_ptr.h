#ifndef __MARKED_SHARED_PTR_H__
#define __MARKED_SHARED_PTR_H__

#include <lu/shared_ptr.h>
#include <lu/utils/marked_ptr.h>

#include <memory>
#include <type_traits>
#include <utility>


namespace lu {
namespace detail {

template <class>
class MarkedSharedPointerTraits;

}// namespace detail

template <class ValueType>
class marked_shared_ptr : public detail::StrongPointer<ValueType *, marked_ptr<detail::ControlBlock>> {
    using Base = detail::StrongPointer<ValueType *, marked_ptr<detail::ControlBlock>>;

    template <class>
    friend class marked_shared_ptr;

    template <class>
    friend class detail::MarkedSharedPointerTraits;

public:
    using element_type = typename Base::element_type;
    using control_block_ptr = typename Base::control_block_ptr;
    using element_ptr = typename Base::element_ptr;

private:
    explicit marked_shared_ptr(control_block_ptr control_block) noexcept {
        this->SetData(reinterpret_cast<element_ptr>(control_block->get()), control_block);
    }

public:
    marked_shared_ptr() noexcept = default;

    marked_shared_ptr(std::nullptr_t) noexcept {}

    template <class _ValueType, class Deleter = std::default_delete<_ValueType>,
              class Allocator = std::allocator<_ValueType>,
              class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
    explicit marked_shared_ptr(_ValueType *value_ptr, Deleter deleter = {}, const Allocator &allocator = {}) {
        Construct(value_ptr, std::move(deleter), allocator);
    }

    marked_shared_ptr(const marked_shared_ptr &other) noexcept { this->CopyConstruct(other); }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    marked_shared_ptr(const detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        this->CopyConstruct(other);
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    marked_shared_ptr(const detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &other,
                      bool bit_value) noexcept {
        this->CopyConstruct(other);
        this->control_block_ = control_block_ptr(this->control_block_.get(), bit_value);
    }

    marked_shared_ptr(marked_shared_ptr &&other) noexcept { this->MoveConstruct(std::move(other)); }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    marked_shared_ptr(detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        this->MoveConstruct(std::move(other));
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    explicit marked_shared_ptr(const detail::WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        this->ConstructFromWeak(other);
    }

    ~marked_shared_ptr() { this->DecRef(); }

    marked_shared_ptr &operator=(const marked_shared_ptr &other) noexcept {
        marked_shared_ptr temp(other);
        this->swap(temp);
        return *this;
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    marked_shared_ptr &operator=(const detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        marked_shared_ptr temp(other);
        this->swap(temp);
        return *this;
    }

    marked_shared_ptr &operator=(marked_shared_ptr &&other) noexcept {
        marked_shared_ptr temp(std::move(other));
        this->swap(temp);
        return *this;
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    marked_shared_ptr &operator=(detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        marked_shared_ptr temp(std::move(other));
        this->swap(temp);
        return *this;
    }

    template <class _ValueType, class Deleter = std::default_delete<_ValueType>,
              class Allocator = std::allocator<_ValueType>,
              class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
    void reset(_ValueType *value_ptr = {}, Deleter deleter = {}, const Allocator &allocator = {}) noexcept {
        marked_shared_ptr temp(value_ptr, std::move(deleter), allocator);
        this->swap(temp);
    }

    void mark() noexcept { this->control_block_.mark(); }

    void unmark() noexcept { this->control_block_.unmark(); }

    bool is_marked() const noexcept { return this->control_block_.is_marked(); }

    friend bool operator==(const marked_shared_ptr &left, const marked_shared_ptr &right) noexcept {
        return left.get() == right.get() && left.control_block_ == right.control_block_;
    }

    friend bool operator!=(const marked_shared_ptr &left, const marked_shared_ptr &right) noexcept {
        return !(left == right);
    }

private:
    template <class _ValueType, class Deleter = std::default_delete<_ValueType>,
              class Allocator = std::allocator<_ValueType>>
    void Construct(_ValueType *value_ptr, Deleter deleter = {}, const Allocator &allocator = {}) {
        auto control_block
                = make_outplace_control_block<_ValueType>(value_ptr, std::move(deleter), allocator);
        this->SetData(value_ptr, control_block);
    }
};

}// namespace lu

#endif
