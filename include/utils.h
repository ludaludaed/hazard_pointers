#ifndef __UTILS_H__
#define __UTILS_H__

#include <utility>
#include <memory>

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
}// namespace lu

#endif