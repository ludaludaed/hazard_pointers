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

inline lu::hazard_pointer_domain& get_ref_count_domain() noexcept {
    static lu::hazard_pointer_domain domain(1);
    return domain;
}

// namespase hide for resolve this problem
// error: 'ControlBlockDeleter' is a private member of 'lu::detail::ControlBlockDeleter'
namespace hide {

struct ControlBlockDeleter {
    template<class ControlBlock>
    void operator()(ControlBlock *ptr) {
        ptr->DeleteControlBlock();
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
    inline bool IncRefIfNotZero(std::int64_t num = 1) noexcept {
        std::int64_t expected = ref_count_.load(std::memory_order_relaxed);
        while (expected != 0) {
            if (ref_count_.compare_exchange_weak(expected, expected + num, std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    inline void IncRef(std::int64_t num = 1) noexcept {
        ref_count_.fetch_add(num, std::memory_order_relaxed);
    }

    inline void IncWeak(std::int64_t num = 1) noexcept {
        weak_count_.fetch_add(num, std::memory_order_relaxed);
    }

    inline void DecRef(std::int64_t num = 1) {
        if (ref_count_.fetch_sub(num, std::memory_order_acq_rel) <= num) {
            DestroyControlBlock();
        }
    }

    inline void DecWeak(std::int64_t num = 1) noexcept {
        if (weak_count_.fetch_sub(num, std::memory_order_acq_rel) <= num) {
            this->retire({}, get_ref_count_domain());
        }
    }

    std::int64_t use_count() const noexcept {
        return ref_count_.load(std::memory_order_relaxed);
    }

    virtual void *get() const noexcept = 0;

private:
    virtual void DeleteValue() = 0;

    virtual void DeleteControlBlock() = 0;

    void DestroyControlBlock() {
        thread_local lu::forward_list<ControlBlock> list{};
        thread_local bool in_progress{false};

        list.push_front(*this);
        if (!in_progress) {
            in_progress = true;
            while (!list.empty()) {
                auto &popped = list.front();
                list.pop_front();
                popped.DeleteValue();
                popped.DecWeak();
            }
            in_progress = false;
        }
    }

private:
    std::atomic<std::int64_t> ref_count_{1};
    std::atomic<std::int64_t> weak_count_{1};
};

template<class ValueType, class Deleter, class Allocator>
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

    void *get() const noexcept override {
        return reinterpret_cast<void *>(lu::to_raw_pointer(value_ptr_));
    }

private:
    void DeleteValue() override {
        deleter_(value_ptr_);
    }

    void DeleteControlBlock() override {
        allocator_type copy = allocator_;
        allocator_traits::destroy(copy, this);
        allocator_traits::deallocate(copy, this, 1);
    }

private:
    pointer value_ptr_;
    deleter_type deleter_;
    allocator_type allocator_;
};

template<class ValueType, class Allocator>
class InplaceControlBlock : public ControlBlock {
public:
    using allocator_type =
            typename std::allocator_traits<Allocator>::template rebind_alloc<InplaceControlBlock<ValueType, Allocator>>;
    using allocator_traits = std::allocator_traits<allocator_type>;

    using value_type = ValueType;
    using pointer = value_type *;

public:
    template<class... Args>
    InplaceControlBlock(const Allocator &allocator, Args &&...args)
        : allocator_(allocator)
        , data_(std::forward<Args>(args)...) {}

    void *get() const noexcept override {
        return data_.get_ptr();
    }

private:
    void DeleteValue() noexcept override {
        data_.destroy();
    }

    void DeleteControlBlock() override {
        allocator_type copy = allocator_;
        allocator_traits::destroy(copy, this);
        allocator_traits::deallocate(copy, this, 1);
    }

private:
    AlignedStorage<ValueType> data_;
    allocator_type allocator_;
};

template<class ValueType, class Deleter, class Allocator>
OutplaceControlBlock<ValueType, Deleter, Allocator> *make_outplace_control_block(ValueType *value_ptr, Deleter deleter,
                                                                                 const Allocator allocator) {
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

template<class ValueType, class Allocator, class... Args>
InplaceControlBlock<ValueType, Allocator> *make_inplace_control_block(const Allocator &allocator, Args &&...args) {
    using ControlBlock = InplaceControlBlock<ValueType, Allocator>;
    using AllocatorType = typename std::allocator_traits<Allocator>::template rebind_alloc<ControlBlock>;
    using AllocatorTraits = std::allocator_traits<AllocatorType>;

    AllocatorType internal_allocator(allocator);

    AllocateGuard allocate_guard(internal_allocator);

    auto result = allocate_guard.allocate();
    AllocatorTraits::construct(internal_allocator, result, allocator, std::forward<Args>(args)...);

    return allocate_guard.release();
}

template<class ValuePtr, class ControlBlockPtr>
class StrongPointer;

template<class ValuePtr, class ControlBlockPtr>
class WeakPointer;

template<class ValuePtr, class ControlBlockPtr>
class StrongPointer {

    template<class, class>
    friend class StrongPointer;

    template<class, class>
    friend class WeakPointer;

public:
    using element_type = typename std::pointer_traits<ValuePtr>::element_type;
    using control_block_ptr = ControlBlockPtr;
    using element_ptr = ValuePtr;

public:
    StrongPointer() noexcept = default;

    ~StrongPointer() = default;

    void swap(StrongPointer &other) noexcept {
        std::swap(value_, other.value_);
        std::swap(control_block_, other.control_block_);
    }

    friend void swap(StrongPointer &left, StrongPointer &right) noexcept {
        left.swap(right);
    }

    [[nodiscard]] long use_count() const noexcept {
        if (control_block_) {
            return control_block_->use_count();
        } else {
            return 0;
        }
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                      && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    bool owner_before(const StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        return control_block_ < other.control_block_;
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                      && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    bool owner_before(const WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        return control_block_ < other.control_block_;
    }

    element_ptr get() const noexcept {
        return value_;
    }

    explicit operator bool() const noexcept {
        return this->control_block_;
    }

    std::add_lvalue_reference_t<element_type> operator*() const noexcept {
        return *this->value_;
    }

    element_type *operator->() const noexcept {
        return this->value_;
    }

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
    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                      && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void CopyConstruct(const StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        value_ = other.value_;
        control_block_ = other.control_block_;

        IncRef();
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                      && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void MoveConstruct(StrongPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        value_ = other.value_;
        control_block_ = other.control_block_;

        other.value_ = {};
        other.control_block_ = {};
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                      && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void ConstructFromWeak(const WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        if (other.control_block_ && other.control_block_->IncRefIfNotZero()) {
            control_block_ = other.control_block_;
            value_ = other.value_;
        }
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                      && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void SetData(_ValuePtr value, _ControlBlockPtr control_block) noexcept {
        value_ = value;
        control_block_ = control_block;
    }

    void IncRef() noexcept {
        if (control_block_) {
            control_block_->IncRef();
        }
    }

    void DecRef() noexcept {
        if (control_block_) {
            control_block_->DecRef();
        }
    }

    control_block_ptr GetControlBlock() const noexcept {
        return control_block_;
    }

    control_block_ptr Release() noexcept {
        auto old = control_block_;
        control_block_ = {};
        value_ = {};
        return old;
    }

protected:
    control_block_ptr control_block_{};
    element_ptr value_{};
};

template<class ValuePtr, class ControlBlockPtr>
class WeakPointer {

    template<class, class>
    friend class StrongPointer;

    template<class, class>
    friend class WeakPointer;

public:
    using element_type = typename std::pointer_traits<ValuePtr>::element_type;
    using control_block_ptr = ControlBlockPtr;
    using element_ptr = ValuePtr;

public:
    WeakPointer() noexcept = default;

    ~WeakPointer() = default;

    void swap(WeakPointer &other) noexcept {
        std::swap(value_, other.value_);
        std::swap(control_block_, other.control_block_);
    }

    friend void swap(WeakPointer &left, WeakPointer &right) noexcept {
        left.swap(right);
    }

    [[nodiscard]] long use_count() const noexcept {
        if (control_block_) {
            return control_block_->use_count();
        } else {
            return 0;
        }
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                      && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    bool owner_before(const StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        return control_block_ < other.control_block_;
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                      && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    bool owner_before(const WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        return control_block_ < other.control_block_;
    }

    element_ptr get() const noexcept {
        return value_;
    }

protected:
    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                      && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void CopyConstruct(const WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        if (other.control_block_) {
            control_block_ = other.control_block_;
            control_block_->IncWeak();
            if (control_block_->IncRefIfNotZero()) {// for virtual inheritance
                value_ = other.value_;
                control_block_->DecRef();
            }
        }
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                      && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void MoveConstruct(WeakPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        control_block_ = other.control_block_;
        other.control_block_ = {};
        if (control_block_->IncRefIfNotZero()) {// for virtual inheritance
            value_ = other.value_;
            control_block_->DecWeak();
        }
        other.value_ = {};
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                      && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void ConstructFromStrong(const StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        if (other.control_block_) {
            value_ = other.value_;
            control_block_ = other.control_block_;
            control_block_->IncWeak();
        }
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr>
                                      && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>>>
    void SetData(_ValuePtr value, _ControlBlockPtr control_block) noexcept {
        value_ = value;
        control_block_ = control_block;
    }

    void IncWeak() noexcept {
        if (control_block_) {
            control_block_->IncWeak();
        }
    }

    void DecWeak() noexcept {
        if (control_block_) {
            control_block_->DecWeak();
        }
    }

    control_block_ptr GetControlBlock() const noexcept {
        return control_block_;
    }

    control_block_ptr Release() noexcept {
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

template<class>
class SharedPointerTraits;

}// namespace detail

template<class ValueType>
class shared_ptr;

template<class ValueType>
class weak_ptr;

template<class ValueType>
class shared_ptr : public detail::StrongPointer<ValueType *, detail::ControlBlock *> {
    using Base = detail::StrongPointer<ValueType *, detail::ControlBlock *>;

    template<class _ValueType, class Allocator, class... Args>
    friend shared_ptr<_ValueType> alloc_shared(const Allocator &allocator, Args &&...args);

    template<class _ValueType, class... Args>
    friend shared_ptr<_ValueType> make_shared(Args &&...args);

    template<class>
    friend class shared_ptr;

    template<class>
    friend class detail::SharedPointerTraits;

public:
    using element_type = typename Base::element_type;
    using control_block_ptr = typename Base::control_block_ptr;
    using element_ptr = typename Base::element_ptr;

private:
    explicit shared_ptr(control_block_ptr control_block) {
        this->SetData(reinterpret_cast<element_ptr>(control_block->get()), control_block);
    }

public:
    shared_ptr() noexcept = default;

    shared_ptr(std::nullptr_t) noexcept {}

    template<class _ValueType, class Deleter = std::default_delete<_ValueType>,
             class Allocator = std::allocator<_ValueType>,
             class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
    explicit shared_ptr(_ValueType *value_ptr, Deleter deleter = {}, const Allocator &allocator = {}) {
        Construct(value_ptr, std::move(deleter), allocator);
    }

    shared_ptr(const shared_ptr &other) noexcept {
        this->CopyConstruct(other);
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                      && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    shared_ptr(const detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        this->CopyConstruct(other);
    }

    shared_ptr(shared_ptr &&other) noexcept {
        this->MoveConstruct(std::move(other));
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                      && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    shared_ptr(detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        this->MoveConstruct(std::move(other));
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                      && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    explicit shared_ptr(const detail::WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        this->ConstructFromWeak(other);
    }

    ~shared_ptr() {
        this->DecRef();
    }

    shared_ptr &operator=(const shared_ptr &other) noexcept {
        shared_ptr temp(other);
        this->swap(temp);
        return *this;
    }

    template<class _ValuePtr, class _ControlBlockPtr,
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

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                      && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    shared_ptr &operator=(detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        shared_ptr temp(std::move(other));
        this->swap(temp);
        return *this;
    }

    template<class _ValueType, class Deleter = std::default_delete<_ValueType>,
             class Allocator = std::allocator<_ValueType>,
             class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
    void reset(_ValueType *value_ptr = {}, Deleter deleter = {}, const Allocator &allocator = {}) noexcept {
        shared_ptr temp(value_ptr, std::move(deleter), allocator);
        this->swap(temp);
    }

private:
    template<class _ValueType, class Deleter = std::default_delete<_ValueType>,
             class Allocator = std::allocator<_ValueType>>
    void Construct(_ValueType *value_ptr, Deleter deleter = {}, const Allocator &allocator = {}) {
        auto control_block = make_outplace_control_block<_ValueType>(value_ptr, std::move(deleter), allocator);
        this->SetData(value_ptr, control_block);
    }
};

template<class ValueType>
class weak_ptr : public detail::WeakPointer<ValueType *, detail::ControlBlock *> {
    using Base = detail::WeakPointer<ValueType *, detail::ControlBlock *>;

    template<class>
    friend class shared_ptr;

    template<class>
    friend class weak_ptr;

public:
    using element_type = typename Base::element_type;
    using control_block_ptr = typename Base::control_block_ptr;
    using element_ptr = typename Base::element_ptr;

private:
    explicit weak_ptr(control_block_ptr control_block) noexcept {
        this->SetData(reinterpret_cast<element_ptr>(control_block->get()), control_block);
    }

public:
    weak_ptr() noexcept = default;

    weak_ptr(std::nullptr_t) noexcept {}

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                      && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    explicit weak_ptr(const detail::StrongPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        this->ConstructFromStrong(other);
    }

    weak_ptr(const weak_ptr &other) noexcept {
        this->CopyConstruct(other);
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                      && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    weak_ptr(const detail::WeakPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
        this->CopyConstruct(other);
    }

    weak_ptr(weak_ptr &&other) noexcept {
        this->MoveConstruct(std::move(other));
    }

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                      && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    weak_ptr(detail::WeakPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        this->MoveConstruct(std::move(other));
    }

    ~weak_ptr() {
        this->DecWeak();
    }

    weak_ptr &operator=(const weak_ptr &other) noexcept {
        weak_ptr temp(other);
        this->swap(temp);
        return *this;
    }

    template<class _ValuePtr, class _ControlBlockPtr,
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

    template<class _ValuePtr, class _ControlBlockPtr,
             class = std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr>
                                      && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>>>
    weak_ptr &operator=(detail::WeakPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
        weak_ptr temp(std::move(other));
        this->swap(temp);
        return *this;
    }

    template<class _ValuePtr, class _ControlBlockPtr,
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

    [[nodiscard]] bool expired() const noexcept {
        return !this->use_count();
    }

    shared_ptr<ValueType> lock() const noexcept {
        return shared_ptr<ValueType>(*this);
    }
};

template<class ValueType, class Allocator = std::allocator<ValueType>, class... Args>
shared_ptr<ValueType> alloc_shared(const Allocator &allocator, Args &&...args) {
    using control_block_ptr = typename shared_ptr<ValueType>::control_block_ptr;

    auto control_block = detail::make_inplace_control_block<ValueType>(allocator, std::forward<Args>(args)...);
    auto raw_value_ptr = reinterpret_cast<ValueType *>(control_block->get());

    shared_ptr<ValueType> result;

    result.SetData(raw_value_ptr, control_block);
    return result;
}

template<class ValueType, class... Args>
shared_ptr<ValueType> make_shared(Args &&...args) {
    return alloc_shared<ValueType>(std::allocator<ValueType>{}, std::forward<Args>(args)...);
}

}// namespace lu

#endif
