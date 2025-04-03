#ifndef __SHARED_PTR_H__
#define __SHARED_PTR_H__

#include <lu/detail/utils.h>
#include <lu/hazard_pointer.h>
#include <lu/intrusive/detail/utils.h>
#include <lu/intrusive/forward_list.h>
#include <lu/intrusive/options.h>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>


namespace lu {
namespace detail {

inline lu::hazard_pointer_domain &get_ref_count_domain() noexcept {
    static lu::hazard_pointer_domain domain(1);
    return domain;
}

// namespase hide for resolve this problem
// error: 'ControlBlockDeleter' is a private member of 'lu::detail::ControlBlockDeleter'
namespace hide {

struct ControlBlockDeleter {
    template <class ControlBlock>
    void operator()(ControlBlock *ptr) {
        ptr->delete_control_block();
    }
};

}// namespace hide

class ControlBlock : public lu::forward_list_base_hook<lu::is_auto_unlink<false>>,
                     public lu::hazard_pointer_obj_base<ControlBlock, hide::ControlBlockDeleter> {
    friend struct hide::ControlBlockDeleter;

public:
    ControlBlock() noexcept = default;

    virtual ~ControlBlock() = default;

public:
    inline bool inc_ref_if_not_zero(std::int64_t num = 1) noexcept {
        std::int64_t expected = ref_count_.load(std::memory_order_relaxed);
        while (expected != 0) {
            if (ref_count_.compare_exchange_weak(expected, expected + num, std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    inline void inc_ref(std::int64_t num = 1) noexcept {
        ref_count_.fetch_add(num, std::memory_order_relaxed);
    }

    inline void inc_weak(std::int64_t num = 1) noexcept {
        weak_count_.fetch_add(num, std::memory_order_relaxed);
    }

    inline void dec_ref(std::int64_t num = 1) {
        if (ref_count_.fetch_sub(num, std::memory_order_acq_rel) <= num) {
            destroy_control_block();
        }
    }

    inline void dec_weak(std::int64_t num = 1) noexcept {
        if (weak_count_.fetch_sub(num, std::memory_order_acq_rel) <= num) {
            this->retire({}, get_ref_count_domain());
        }
    }

    std::int64_t use_count() const noexcept { return ref_count_.load(std::memory_order_relaxed); }

    virtual void *get() const noexcept = 0;

private:
    virtual void delete_value() = 0;

    virtual void delete_control_block() = 0;

    void destroy_control_block() {
        thread_local lu::forward_list<ControlBlock> list{};
        thread_local bool in_progress{false};

        list.push_front(*this);
        if (!in_progress) {
            in_progress = true;
            while (!list.empty()) {
                auto &popped = list.front();
                list.pop_front();
                popped.delete_value();
                popped.dec_weak();
            }
            in_progress = false;
        }
    }

private:
    std::atomic<std::int64_t> ref_count_{1};
    std::atomic<std::int64_t> weak_count_{1};
};

template <class ValueType, class Deleter, class Allocator>
class OutplaceControlBlock : public ControlBlock {
public:
    using value_type = ValueType;
    using pointer = ValueType *;

    using deleter_type = Deleter;
    using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<
            OutplaceControlBlock<value_type, Deleter, Allocator>>;
    using allocator_traits = std::allocator_traits<allocator_type>;

public:
    OutplaceControlBlock(pointer value_ptr, deleter_type deleter, const Allocator &allocator) noexcept
        : value_ptr_(value_ptr)
        , deleter_(std::move(deleter))
        , allocator_(allocator) {}

    void *get() const noexcept override { return reinterpret_cast<void *>(lu::to_raw_pointer(value_ptr_)); }

private:
    void delete_value() override { deleter_(value_ptr_); }

    void delete_control_block() override {
        allocator_type copy = allocator_;
        allocator_traits::destroy(copy, this);
        allocator_traits::deallocate(copy, this, 1);
    }

private:
    pointer value_ptr_;
    deleter_type deleter_;
    allocator_type allocator_;
};

template <class ValueType, class Allocator>
class InplaceControlBlock : public ControlBlock {
public:
    using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<
            InplaceControlBlock<ValueType, Allocator>>;
    using allocator_traits = std::allocator_traits<allocator_type>;

    using value_type = ValueType;
    using pointer = value_type *;

public:
    template <class... Args>
    InplaceControlBlock(const Allocator &allocator, Args &&...args)
        : allocator_(allocator)
        , data_(std::forward<Args>(args)...) {}

    void *get() const noexcept override { return data_.get_ptr(); }

private:
    void delete_value() noexcept override { data_.destroy(); }

    void delete_control_block() override {
        allocator_type copy = allocator_;
        allocator_traits::destroy(copy, this);
        allocator_traits::deallocate(copy, this, 1);
    }

private:
    AlignedStorage<ValueType> data_;
    allocator_type allocator_;
};

template <class ValueType, class Deleter, class Allocator>
OutplaceControlBlock<ValueType, Deleter, Allocator> *
        make_outplace_control_block(ValueType *value_ptr, Deleter deleter, const Allocator allocator) {
    using ControlBlock = OutplaceControlBlock<ValueType, Deleter, Allocator>;
    using AllocatorType = typename std::allocator_traits<Allocator>::template rebind_alloc<ControlBlock>;
    using AllocatorTraits = std::allocator_traits<AllocatorType>;

    AllocatorType internal_allocator(allocator);

    DeleterGuard deleter_guard(value_ptr, deleter);
    AllocateGuard allocate_guard(internal_allocator);

    auto result = allocate_guard.allocate();
    AllocatorTraits::construct(internal_allocator, result, value_ptr, std::move(deleter), allocator);

    deleter_guard.release();
    return allocate_guard.release();
}

template <class ValueType, class Allocator, class... Args>
InplaceControlBlock<ValueType, Allocator> *make_inplace_control_block(const Allocator &allocator,
                                                                      Args &&...args) {
    using ControlBlock = InplaceControlBlock<ValueType, Allocator>;
    using AllocatorType = typename std::allocator_traits<Allocator>::template rebind_alloc<ControlBlock>;
    using AllocatorTraits = std::allocator_traits<AllocatorType>;

    AllocatorType internal_allocator(allocator);

    AllocateGuard allocate_guard(internal_allocator);

    auto result = allocate_guard.allocate();
    AllocatorTraits::construct(internal_allocator, result, allocator, std::forward<Args>(args)...);

    return allocate_guard.release();
}

template <class ValuePtr, class ControlBlockPtr>
class StrongPointer;

template <class ValuePtr, class ControlBlockPtr>
class WeakPointer;

template <class ValuePtr, class ControlBlockPtr>
class StrongPointer {

    template <class, class>
    friend class StrongPointer;

    template <class, class>
    friend class WeakPointer;

public:
    using element_type = typename std::pointer_traits<ValuePtr>::element_type;
    using control_block_ptr = ControlBlockPtr;
    using element_ptr = ValuePtr;

public:
    StrongPointer() noexcept = default;

    ~StrongPointer() = default;

    [[nodiscard]] long use_count() const noexcept {
        if (control_block_) {
            return control_block_->use_count();
        } else {
            return 0;
        }
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                       && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    bool owner_before(const StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        return control_block_ < other.control_block_;
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                       && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    bool owner_before(const WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        return control_block_ < other.control_block_;
    }

    element_ptr get() const noexcept { return value_; }

    explicit operator bool() const noexcept { return this->control_block_; }

    std::add_lvalue_reference_t<element_type> operator*() const noexcept { return *this->value_; }

    element_type *operator->() const noexcept { return this->value_; }

    friend bool operator==(const StrongPointer &left, const StrongPointer &right) noexcept {
        return left.get() == right.get();
    }

    friend bool operator!=(const StrongPointer &left, const StrongPointer &right) noexcept {
        return !(left == right);
    }

    friend bool operator<(const StrongPointer &left, const StrongPointer &right) noexcept {
        return left.get() < right.get();
    }

    friend bool operator>(const StrongPointer &left, const StrongPointer &right) noexcept {
        return right < left;
    }

    friend bool operator<=(const StrongPointer &left, const StrongPointer &right) noexcept {
        return !(right < left);
    }

    friend bool operator>=(const StrongPointer &left, const StrongPointer &right) noexcept {
        return !(left < right);
    }

protected:
    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                       && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void copy_construct(const StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        value_ = other.value_;
        control_block_ = other.control_block_;

        inc_ref();
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                       && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void move_construct(StrongPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        value_ = other.value_;
        control_block_ = other.control_block_;

        other.value_ = {};
        other.control_block_ = {};
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                       && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void construct_from_weak(const WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        if (other.control_block_ && other.control_block_->inc_ref_if_not_zero()) {
            control_block_ = other.control_block_;
            value_ = other.value_;
        }
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                       && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void set_data(_ValuePtr value, _ControlBlockPtr control_block) noexcept {
        value_ = value;
        control_block_ = control_block;
    }

    void inc_ref() noexcept {
        if (control_block_) {
            control_block_->inc_ref();
        }
    }

    void dec_ref() noexcept {
        if (control_block_) {
            control_block_->dec_ref();
        }
    }

    control_block_ptr get_control_block() const noexcept { return control_block_; }

    control_block_ptr release() noexcept {
        auto old = control_block_;
        control_block_ = {};
        value_ = {};
        return old;
    }

protected:
    control_block_ptr control_block_{};
    element_ptr value_{};
};

template <class ValuePtr, class ControlBlockPtr>
class WeakPointer {

    template <class, class>
    friend class StrongPointer;

    template <class, class>
    friend class WeakPointer;

public:
    using element_type = typename std::pointer_traits<ValuePtr>::element_type;
    using control_block_ptr = ControlBlockPtr;
    using element_ptr = ValuePtr;

public:
    WeakPointer() noexcept = default;

    ~WeakPointer() = default;

    [[nodiscard]] long use_count() const noexcept {
        if (control_block_) {
            return control_block_->use_count();
        } else {
            return 0;
        }
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                       && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    bool owner_before(const StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        return control_block_ < other.control_block_;
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                       && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    bool owner_before(const WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        return control_block_ < other.control_block_;
    }

    element_ptr get() const noexcept { return value_; }

protected:
    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                       && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void copy_construct(const WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        if (other.control_block_) {
            control_block_ = other.control_block_;
            control_block_->inc_weak();
            if (control_block_->inc_ref_if_not_zero()) {// for virtual inheritance
                value_ = other.value_;
                control_block_->dec_ref();
            }
        }
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                       && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void move_construct(WeakPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        control_block_ = other.control_block_;
        other.control_block_ = {};
        if (control_block_->inc_ref_if_not_zero()) {// for virtual inheritance
            value_ = other.value_;
            control_block_->dec_weak();
        }
        other.value_ = {};
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                       && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void construct_from_strong(const StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        if (other.control_block_) {
            value_ = other.value_;
            control_block_ = other.control_block_;
            control_block_->inc_weak();
        }
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                       && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void set_data(_ValuePtr value, _ControlBlockPtr control_block) noexcept {
        value_ = value;
        control_block_ = control_block;
    }

    void inc_weak() noexcept {
        if (control_block_) {
            control_block_->inc_weak();
        }
    }

    void dec_weak() noexcept {
        if (control_block_) {
            control_block_->dec_weak();
        }
    }

    control_block_ptr get_control_block() const noexcept { return control_block_; }

    control_block_ptr release() noexcept {
        auto old = control_block_;
        control_block_ = {};
        value_ = {};
        return old;
    }

private:
    control_block_ptr control_block_{};
    element_ptr value_{};
};

}// namespace detail

namespace detail {

template <class>
class SharedPointerTraits;

}// namespace detail

template <class ValueType>
class shared_ptr;

template <class ValueType>
class weak_ptr;

template <class ValueType>
class shared_ptr : public detail::StrongPointer<ValueType *, detail::ControlBlock *> {
    using Base = detail::StrongPointer<ValueType *, detail::ControlBlock *>;

    template <class _ValueType, class Allocator, class... Args>
    friend shared_ptr<_ValueType> alloc_shared(const Allocator &allocator, Args &&...args);

    template <class _ValueType, class... Args>
    friend shared_ptr<_ValueType> make_shared(Args &&...args);

    template <class>
    friend class shared_ptr;

    template <class>
    friend class detail::SharedPointerTraits;

public:
    using element_type = typename Base::element_type;
    using control_block_ptr = typename Base::control_block_ptr;
    using element_ptr = typename Base::element_ptr;

private:
    explicit shared_ptr(control_block_ptr control_block) {
        this->set_data(static_cast<element_ptr>(control_block->get()), control_block);
    }

public:
    shared_ptr() noexcept = default;

    shared_ptr(std::nullptr_t) noexcept {}

    template <class _ValueType, class Deleter = std::default_delete<_ValueType>,
              class Allocator = std::allocator<_ValueType>,
              class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
    explicit shared_ptr(_ValueType *value_ptr, Deleter deleter = {}, const Allocator &allocator = {}) {
        construct(value_ptr, std::move(deleter), allocator);
    }

    shared_ptr(const shared_ptr &other) noexcept { this->copy_construct(other); }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    shared_ptr(const detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        this->copy_construct(other);
    }

    shared_ptr(shared_ptr &&other) noexcept { this->move_construct(std::move(other)); }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    shared_ptr(detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        this->move_construct(std::move(other));
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    explicit shared_ptr(const detail::WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        this->construct_from_weak(other);
    }

    ~shared_ptr() { this->dec_ref(); }

    shared_ptr &operator=(const shared_ptr &other) noexcept {
        shared_ptr temp(other);
        this->swap(temp);
        return *this;
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    shared_ptr &operator=(const detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        shared_ptr temp(other);
        this->swap(temp);
        return *this;
    }

    shared_ptr &operator=(shared_ptr &&other) noexcept {
        shared_ptr temp(std::move(other));
        this->swap(temp);
        return *this;
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    shared_ptr &operator=(detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        shared_ptr temp(std::move(other));
        this->swap(temp);
        return *this;
    }

    template <class _ValueType, class Deleter = std::default_delete<_ValueType>,
              class Allocator = std::allocator<_ValueType>,
              class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
    void reset(_ValueType *value_ptr = {}, Deleter deleter = {}, const Allocator &allocator = {}) noexcept {
        shared_ptr temp(value_ptr, std::move(deleter), allocator);
        this->swap(temp);
    }

    void swap(shared_ptr &other) noexcept {
        std::swap(this->value_, other.value_);
        std::swap(this->control_block_, other.control_block_);
    }

    friend void swap(shared_ptr &left, shared_ptr &right) noexcept { left.swap(right); }

private:
    template <class _ValueType, class Deleter = std::default_delete<_ValueType>,
              class Allocator = std::allocator<_ValueType>>
    void construct(_ValueType *value_ptr, Deleter deleter = {}, const Allocator &allocator = {}) {
        auto control_block
                = make_outplace_control_block<_ValueType>(value_ptr, std::move(deleter), allocator);
        this->set_data(value_ptr, control_block);
    }
};

template <class ValueType>
class weak_ptr : public detail::WeakPointer<ValueType *, detail::ControlBlock *> {
    using Base = detail::WeakPointer<ValueType *, detail::ControlBlock *>;

    template <class>
    friend class shared_ptr;

    template <class>
    friend class weak_ptr;

public:
    using element_type = typename Base::element_type;
    using control_block_ptr = typename Base::control_block_ptr;
    using element_ptr = typename Base::element_ptr;

private:
    explicit weak_ptr(control_block_ptr control_block) noexcept {
        this->set_data(static_cast<element_ptr>(control_block->get()), control_block);
    }

public:
    weak_ptr() noexcept = default;

    weak_ptr(std::nullptr_t) noexcept {}

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    explicit weak_ptr(const detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        this->construct_from_strong(other);
    }

    weak_ptr(const weak_ptr &other) noexcept { this->copy_construct(other); }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    weak_ptr(const detail::WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        this->copy_construct(other);
    }

    weak_ptr(weak_ptr &&other) noexcept { this->move_construct(std::move(other)); }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    weak_ptr(detail::WeakPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        this->move_construct(std::move(other));
    }

    ~weak_ptr() { this->dec_weak(); }

    weak_ptr &operator=(const weak_ptr &other) noexcept {
        weak_ptr temp(other);
        this->swap(temp);
        return *this;
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    weak_ptr &operator=(const detail::WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        weak_ptr temp(other);
        this->swap(temp);
        return *this;
    }

    weak_ptr &operator=(weak_ptr &&other) noexcept {
        weak_ptr temp(std::move(other));
        this->swap(temp);
        return *this;
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    weak_ptr &operator=(detail::WeakPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        weak_ptr temp(std::move(other));
        this->swap(temp);
        return *this;
    }

    template <class _ValuePtr, class _ControlBlockPtr,
              class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                       && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    weak_ptr &operator=(const detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        weak_ptr temp(other);
        this->swap(temp);
        return *this;
    }

    void reset() noexcept {
        weak_ptr temp;
        this->swap(temp);
    }

    void swap(weak_ptr &other) noexcept {
        std::swap(this->value_, other.value_);
        std::swap(this->control_block_, other.control_block_);
    }

    friend void swap(weak_ptr &left, weak_ptr &right) noexcept { left.swap(right); }

    [[nodiscard]] bool expired() const noexcept { return !this->use_count(); }

    shared_ptr<ValueType> lock() const noexcept { return shared_ptr<ValueType>(*this); }
};

template <class ValueType, class Allocator = std::allocator<ValueType>, class... Args>
shared_ptr<ValueType> alloc_shared(const Allocator &allocator, Args &&...args) {
    using control_block_ptr = typename shared_ptr<ValueType>::control_block_ptr;

    auto control_block
            = detail::make_inplace_control_block<ValueType>(allocator, std::forward<Args>(args)...);
    auto raw_value_ptr = reinterpret_cast<ValueType *>(control_block->get());

    shared_ptr<ValueType> result;

    result.set_data(raw_value_ptr, control_block);
    return result;
}

template <class ValueType, class... Args>
shared_ptr<ValueType> make_shared(Args &&...args) {
    return alloc_shared<ValueType>(std::allocator<ValueType>{}, std::forward<Args>(args)...);
}

}// namespace lu

#endif
