#ifndef __SHARED_PTR_H__
#define __SHARED_PTR_H__

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

#include "atomic_shared_pointer.h"
#include "hazard_pointer.h"
#include "intrusive/forward_list.h"
#include "intrusive/options.h"
#include "intrusive/utils.h"

namespace lu {
    template<class ValueType>
    class AlignedStorage {
    public:
        using reference = ValueType &;
        using pointer = ValueType *;
        using const_pointer = const ValueType *;

    public:
        AlignedStorage() = default;

        template<class... Args>
        AlignedStorage(Args &&...args) {
            emplace(std::forward<Args>(args)...);
        }

    public:
        template<class... Args>
        void emplace(Args &&...args) {
            ::new (data_) ValueType(std::forward<Args>(args)...);
        }

        void destroy() {
            reinterpret_cast<ValueType *>(data_)->~ValueType();
        }

        reference operator*() {
            return *reinterpret_cast<pointer>(data_);
        }

        pointer operator->() const {
            return const_cast<pointer>(reinterpret_cast<const_pointer>(data_));
        }

        void *get_ptr() const {
            return reinterpret_cast<void *>(this->operator->());
        }

    private:
        alignas(alignof(ValueType)) unsigned char data_[sizeof(ValueType)]{};
    };

    template<class Allocator>
    class AllocateGuard {
        using allocator_traits = std::allocator_traits<Allocator>;
        using pointer = typename std::allocator_traits<Allocator>::pointer;

    public:
        explicit AllocateGuard(Allocator &allocator) noexcept
            : allocator_(allocator) {}

        ~AllocateGuard() {
            if (ptr_) {
                allocator_traits::deallocate(allocator_, ptr_, 1);
            }
        }

        pointer allocate() {
            ptr_ = allocator_traits::allocate(allocator_, 1);
            return ptr_;
        }

        pointer release() {
            return std::exchange(ptr_, pointer{});
        }

    private:
        Allocator &allocator_;
        pointer ptr_{};
    };

    template<class Ptr, class Deleter>
    class DeleterGuard {
    public:
        DeleterGuard(Ptr ptr, Deleter &deleter) noexcept
            : ptr_(ptr), deleter_(deleter) {}

        ~DeleterGuard() {
            if (ptr_) {
                deleter_(ptr_);
            }
        }

        Ptr release() {
            return std::exchange(ptr_, Ptr{});
        }

    private:
        Ptr ptr_;
        Deleter &deleter_;
    };

    struct ControlBlockDeleter {
        template<class ControlBlock>
        void operator()(ControlBlock *ptr) {
            ptr->do_retire_deleter();
        }
    };

    class ControlBlock : public lu::forward_list_hook<lu::is_auto_unlink<false>>,
                         public lu::hazard_pointer_obj_base<ControlBlock, ControlBlockDeleter> {
    public:
        friend struct ControlBlockDeleter;

    public:
        ControlBlock() = default;

        virtual ~ControlBlock() {};

    public:
        inline bool inc_ref_if_not_zero(std::int64_t num = 1) noexcept {
            std::int64_t expected = ref_count_.load();
            while (expected != 0) {
                if (ref_count_.compare_exchange_weak(expected, expected + num)) {
                    return true;
                }
            }
            return false;
        }

        inline bool inc_weak_if_not_zero(std::int64_t num = 1) noexcept {
            std::int64_t expected = weak_count_.load();
            while (expected != 0) {
                if (weak_count_.compare_exchange_weak(expected, expected + num)) {
                    return true;
                }
            }
            return false;
        }

        inline void inc_ref(std::int64_t num = 1) noexcept {
            ref_count_.fetch_add(num);
        }

        inline void inc_weak(std::int64_t num = 1) noexcept {
            weak_count_.fetch_add(num);
        }

        inline void dec_ref(std::int64_t num = 1) noexcept {
            if (ref_count_.fetch_sub(num) <= num) {
                destroy_control_block();
            }
        }

        inline void dec_weak(std::int64_t num = 1) noexcept {
            if (weak_count_.fetch_sub(num) <= num) {
                delete_this();
            }
        }

        std::int64_t use_count() const {
            return ref_count_.load();
        }

        virtual void *get() const noexcept = 0;

    private:
        virtual void delete_this() noexcept = 0;

        virtual void dispose() noexcept = 0;

        virtual void do_retire_deleter() = 0;

        void destroy_control_block() {
            thread_local lu::forward_list<ControlBlock> list{};
            thread_local bool in_progress{false};

            list.push_front(*this);
            if (!in_progress) {
                in_progress = true;
                while (!list.empty()) {
                    auto &popped = list.front();
                    list.pop_front();
                    popped.dispose();
                    popped.dec_weak();
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
        using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<OutplaceControlBlock<value_type, Deleter, Allocator>>;
        using allocator_traits = std::allocator_traits<allocator_type>;

    public:
        OutplaceControlBlock(pointer value_ptr, deleter_type deleter, const Allocator &allocator) noexcept
            : value_ptr_(value_ptr),
              deleter_(std::move(deleter)),
              allocator_(allocator) {}

        void *get() const noexcept override {
            return reinterpret_cast<void *>(lu::to_raw_pointer(value_ptr_));
        }

    private:
        void delete_this() noexcept override {
            this->retire();
        }

        void dispose() noexcept override {
            deleter_(value_ptr_);
        }

        void do_retire_deleter() override {
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
        using allocator_type = typename std::allocator_traits<Allocator>::template rebind_alloc<InplaceControlBlock<ValueType, Allocator>>;
        using allocator_traits = std::allocator_traits<allocator_type>;

        using value_type = ValueType;
        using pointer = value_type *;

    public:
        template<class... Args>
        InplaceControlBlock(const Allocator &allocator, Args &&...args)
            : allocator_(allocator),
              data_(std::forward<Args>(args)...) {
        }

        void *get() const noexcept override {
            return data_.get_ptr();
        }

    private:
        void delete_this() noexcept override {
            this->retire();
        }

        void dispose() noexcept override {
            data_.destroy();
        }

        void do_retire_deleter() override {
            allocator_type copy = allocator_;
            allocator_traits::destroy(copy, this);
            allocator_traits::deallocate(copy, this, 1);
        }

    private:
        AlignedStorage<ValueType> data_;
        allocator_type allocator_;
    };

    template<class ValueType, class Deleter, class Allocator>
    OutplaceControlBlock<ValueType, Deleter, Allocator> *make_outplace_control_block(ValueType *value_ptr, Deleter deleter, const Allocator allocator) {
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
    class StrongRefCountPointer;

    template<class ValuePtr, class ControlBlockPtr>
    class WeakRefCountPointer;

    template<class ValuePtr, class ControlBlockPtr>
    class StrongRefCountPointer {

        template<class, class>
        friend class StrongRefCountPointer;

        template<class, class>
        friend class WeakRefCountPointer;

    public:
        using element_type = typename std::pointer_traits<ValuePtr>::element_type;
        using control_block_type = typename std::pointer_traits<ControlBlockPtr>::element_type;

        using control_block_ptr = ControlBlockPtr;
        using element_ptr = ValuePtr;

    public:
        StrongRefCountPointer() = default;

        ~StrongRefCountPointer() = default;

        void swap(StrongRefCountPointer &other) noexcept {
            std::swap(value_, other.value_);
            std::swap(control_block_, other.control_block_);
        }

        friend void swap(StrongRefCountPointer &left, StrongRefCountPointer &right) noexcept {
            left.swap(right);
        }

        [[nodiscard]] long use_count() const noexcept {
            if (control_block_) {
                return control_block_->use_count();
            } else {
                return 0;
            }
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr> && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>, int> = 0>
        bool owner_before(const StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) {
            return control_block_ < other.control_block_;
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr> && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>, int> = 0>
        bool owner_before(const WeakRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) {
            return control_block_ < other.control_block_;
        }

        element_ptr get() const noexcept {
            return value_;
        }

        explicit operator bool() const noexcept {
            return this->control_block_;
        }

        element_type &operator*() const noexcept {
            return *this->value_;
        }

        element_type *operator->() const noexcept {
            return this->value_;
        }

        friend bool operator==(const StrongRefCountPointer &left, const StrongRefCountPointer &right) noexcept {
            return left.get() == right.get();
        }

        friend bool operator!=(const StrongRefCountPointer &left, const StrongRefCountPointer &right) noexcept {
            return !(left == right);
        }

        friend bool operator<(const StrongRefCountPointer &left, const StrongRefCountPointer &right) noexcept {
            return left.get() < right.get();
        }

        friend bool operator>(const StrongRefCountPointer &left, const StrongRefCountPointer &right) noexcept {
            return right < left;
        }

        friend bool operator<=(const StrongRefCountPointer &left, const StrongRefCountPointer &right) noexcept {
            return !(right < left);
        }

        friend bool operator>=(const StrongRefCountPointer &left, const StrongRefCountPointer &right) noexcept {
            return !(left < right);
        }

    protected:
        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr> && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>, int> = 0>
        void CopyConstruct(const StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
            value_ = other.value_;
            control_block_ = other.control_block_;

            IncRef();
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr> && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>, int> = 0>
        void MoveConstruct(StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
            value_ = other.value_;
            control_block_ = other.control_block_;

            other.value_ = {};
            other.control_block_ = {};
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr> && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>, int> = 0>
        void ConstructFromWeak(const WeakRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) {
            if (other.control_block_ && other.control_block_->inc_ref_if_not_zero()) {
                control_block_ = other.control_block_;
                value_ = other.value_;
            }
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr> && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>, int> = 0>
        void SetData(_ValuePtr value, _ControlBlockPtr control_block) noexcept {
            value_ = value;
            control_block_ = control_block;
        }

        void IncRef() noexcept {
            if (control_block_) {
                control_block_->inc_ref();
            }
        }

        void DecRef() noexcept {
            if (control_block_) {
                control_block_->dec_ref();
            }
        }

        control_block_ptr get_control_block() const noexcept {
            return control_block_;
        }

        control_block_ptr release() {
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
    class WeakRefCountPointer {

        template<class, class>
        friend class StrongRefCountPointer;

        template<class, class>
        friend class WeakRefCountPointer;

    public:
        using element_type = typename std::pointer_traits<ValuePtr>::element_type;
        using control_block_type = typename std::pointer_traits<ControlBlockPtr>::element_type;

        using control_block_ptr = ControlBlockPtr;
        using element_ptr = ValuePtr;

    public:
        WeakRefCountPointer() = default;

        ~WeakRefCountPointer() = default;

        void swap(WeakRefCountPointer &other) noexcept {
            std::swap(value_, other.value_);
            std::swap(control_block_, other.control_block_);
        }

        friend void swap(WeakRefCountPointer &left, WeakRefCountPointer &right) noexcept {
            left.swap(right);
        }

        [[nodiscard]] long use_count() const noexcept {
            if (control_block_) {
                return control_block_->use_count();
            } else {
                return 0;
            }
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr> && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>, int> = 0>
        bool owner_before(const StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) {
            return control_block_ < other.control_block_;
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr> && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>, int> = 0>
        bool owner_before(const WeakRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) {
            return control_block_ < other.control_block_;
        }

        element_ptr get() const noexcept {
            return value_;
        }

    protected:
        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr> && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>, int> = 0>
        void CopyConstruct(const WeakRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
            if (other.control_block_) {
                value_ = other.value_;
                control_block_ = other.control_block_;
                control_block_->inc_weak();
            }
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr> && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>, int> = 0>
        void MoveConstruct(WeakRefCountPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
            value_ = other.value_;
            control_block_ = other.control_block_;

            other.value_ = {};
            other.control_block_ = {};
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr> && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>, int> = 0>
        void ConstructFromStrong(const StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
            if (other.control_block_) {
                value_ = other.value_;
                control_block_ = other.control_block_;
                control_block_->inc_weak();
            }
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, ValuePtr> && std::is_convertible_v<_ControlBlockPtr, ControlBlockPtr>, int> = 0>
        void SetData(_ValuePtr value, _ControlBlockPtr control_block) noexcept {
            value_ = value;
            control_block_ = control_block;
        }

        void IncWeak() noexcept {
            if (control_block_) {
                control_block_->inc_ref();
            }
        }

        void DecWeak() noexcept {
            if (control_block_) {
                control_block_->dec_ref();
            }
        }

        control_block_ptr get_control_block() const noexcept {
            return control_block_;
        }

        control_block_ptr release() {
            auto old = control_block_;
            control_block_ = {};
            value_ = {};
            return old;
        }

    private:
        control_block_ptr control_block_{};
        element_ptr value_{};
    };

    template<class ValueType>
    class SharedPointer;

    template<class ValueType>
    class WeakPointer;

    template<class ValueType>
    class SharedPointer : public StrongRefCountPointer<ValueType *, ControlBlock *> {
        using Base = StrongRefCountPointer<ValueType *, ControlBlock *>;

        template<class _ValueType, class Allocator, class... Args>
        friend SharedPointer<_ValueType> alloc_shared(const Allocator &allocator, Args &&...args);

        template<class _ValueType, class... Args>
        friend SharedPointer<_ValueType> make_shared(Args &&...args);

        template<class>
        friend class SharedPointer;

        template<class>
        friend class SharedPointerTraits;

    public:
        using Base::Base;

        using element_type = typename Base::element_type;
        using control_block_type = typename Base::control_block_type;

        using control_block_ptr = typename Base::control_block_ptr;
        using element_ptr = typename Base::element_ptr;

    private:
        explicit SharedPointer(control_block_ptr control_block) {
            this->SetData(reinterpret_cast<element_ptr>(control_block->get()), control_block);
        }

    public:
        SharedPointer() noexcept = default;

        template<class _ValueType,
                 class Deleter = std::default_delete<_ValueType>,
                 class Allocator = std::allocator<_ValueType>,
                 std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>, int> = 0>
        explicit SharedPointer(_ValueType *value_ptr, Deleter deleter = {}, const Allocator &allocator = {}) noexcept {
            Construct(value_ptr, std::move(deleter), allocator);
        }

        SharedPointer(const SharedPointer &other) noexcept {
            this->CopyConstruct(other);
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>, int> = 0>
        SharedPointer(const StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
            this->CopyConstruct(other);
        }

        SharedPointer(SharedPointer &&other) noexcept {
            this->MoveConstruct(std::move(other));
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>, int> = 0>
        SharedPointer(StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
            this->MoveConstruct(std::move(other));
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>, int> = 0>
        explicit SharedPointer(const WeakRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) {
            this->ConstructFromWeak(other);
        }

        ~SharedPointer() {
            this->DecRef();
        }

        SharedPointer &operator=(const SharedPointer &other) noexcept {
            SharedPointer temp(other);
            this->swap(temp);
            return *this;
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>, int> = 0>
        SharedPointer &operator=(const StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
            SharedPointer temp(other);
            this->swap(temp);
            return *this;
        }

        SharedPointer &operator=(SharedPointer &&other) noexcept {
            SharedPointer temp(std::move(other));
            this->swap(temp);
            return *this;
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>, int> = 0>
        SharedPointer &operator=(StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
            SharedPointer temp(std::move(other));
            this->swap(temp);
            return *this;
        }

        template<class _ValueType,
                 class Deleter = std::default_delete<_ValueType>,
                 class Allocator = std::allocator<_ValueType>,
                 std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>, int> = 0>
        void reset(_ValueType *value_ptr = {}, Deleter deleter = {}, const Allocator &allocator = {}) noexcept {
            SharedPointer temp(value_ptr, std::move(deleter), allocator);
            this->swap(temp);
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

    template<class ValueType, class Allocator = std::allocator<ValueType>, class... Args>
    SharedPointer<ValueType> alloc_shared(const Allocator &allocator, Args &&...args) {
        using shared_ptr = SharedPointer<ValueType>;
        using control_block_ptr = typename shared_ptr::control_block_ptr;

        auto control_block = make_inplace_control_block<ValueType>(allocator, std::forward<Args>(args)...);
        auto raw_value_ptr = reinterpret_cast<ValueType *>(control_block->get());

        SharedPointer<ValueType> result;

        result.SetData(raw_value_ptr, control_block);
        return result;
    }

    template<class ValueType, class... Args>
    SharedPointer<ValueType> make_shared(Args &&...args) {
        return alloc_shared<ValueType>(std::allocator<ValueType>{}, std::forward<Args>(args)...);
    }

    template<class ValueType>
    class WeakPointer : WeakRefCountPointer<ValueType *, ControlBlock *> {
        using Base = WeakRefCountPointer<ValueType *, ControlBlock *>;

        template<class>
        friend class SharedPointer;

        template<class>
        friend class WeakPointer;

    public:
        using Base::Base;

        using element_type = typename Base::element_type;
        using control_block_type = typename Base::control_block_type;

        using control_block_ptr = typename Base::control_block_ptr;
        using element_ptr = typename Base::element_ptr;

    private:
        explicit WeakPointer(control_block_ptr control_block) noexcept {
            this->SetData(reinterpret_cast<element_ptr>(control_block->get()), control_block);
        }

    public:
        WeakPointer() noexcept = default;

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>, int> = 0>
        explicit WeakPointer(const StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
            this->ConstructFromStrong(other);
        }

        WeakPointer(const WeakPointer &other) noexcept {
            this->CopyConstruct(other);
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>, int> = 0>
        WeakPointer(const WeakRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
            this->CopyConstruct(other);
        }

        WeakPointer(WeakPointer &&other) noexcept {
            this->MoveConstruct(std::move(other));
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>, int> = 0>
        WeakPointer(WeakRefCountPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
            this->MoveConstruct(std::move(other));
        }

        ~WeakPointer() {
            this->DecWeak();
        }

        WeakPointer &operator=(const WeakPointer &other) noexcept {
            WeakPointer temp(other);
            this->swap(temp);
            return *this;
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>, int> = 0>
        WeakPointer &operator=(const WeakRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
            WeakPointer temp(other);
            this->swap(temp);
            return *this;
        }

        WeakPointer &operator=(WeakPointer &&other) noexcept {
            WeakPointer temp(std::move(other));
            this->swap(temp);
            return *this;
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>, int> = 0>
        WeakPointer &operator=(WeakRefCountPointer<_ValuePtr, _ControlBlockPtr> &&other) noexcept {
            WeakPointer temp(std::move(other));
            this->swap(temp);
            return *this;
        }

        template<class _ValuePtr,
                 class _ControlBlockPtr,
                 std::enable_if_t<std::is_convertible_v<_ValuePtr, element_ptr> && std::is_convertible_v<_ControlBlockPtr, control_block_ptr>, int> = 0>
        WeakPointer &operator=(const StrongRefCountPointer<_ValuePtr, _ControlBlockPtr> &other) noexcept {
            WeakPointer temp(other);
            this->swap(temp);
            return *this;
        }

        void reset() noexcept {
            WeakPointer temp;
            this->swap(temp);
        }

        [[nodiscard]] bool expired() const noexcept {
            return this->control_block_;
        }

        SharedPointer<ValueType> lock() const noexcept {
            SharedPointer<ValueType> result(*this);
            return std::move(result);
        }
    };

    template<class ValueType>
    struct SharedPointerTraits {
        using ref_count_ptr = SharedPointer<ValueType>;
        using control_block_ptr = typename ref_count_ptr::control_block_ptr;

        static control_block_ptr get_control_block(ref_count_ptr &ptr) {
            return ptr.get_control_block();
        }

        static control_block_ptr release_ptr(ref_count_ptr &ptr) {
            return ptr.release();
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
    using shared_ptr = SharedPointer<ValueType>;

    template<class ValueType>
    using weak_ptr = WeakPointer<ValueType>;

    template<class ValueType>
    using atomic_shared_ptr = AtomicRefCountPointer<SharedPointerTraits<ValueType>>;
}// namespace lu

#endif