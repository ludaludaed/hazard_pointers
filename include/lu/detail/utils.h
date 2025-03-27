#ifndef __UTILS_H__
#define __UTILS_H__

#include <algorithm>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>


#define UNUSED(expr) (void) (expr)

namespace lu {
namespace detail {

template<std::size_t argument, std::size_t base = 2, bool = (argument < base)>
constexpr std::size_t log = 1 + log<argument / base, base>;

template<std::size_t argument, std::size_t base>
constexpr std::size_t log<argument, base, true> = 0;


struct PointerHash {
    template<class T>
    std::size_t operator()(T *p) const noexcept {
        std::uintptr_t ptr = reinterpret_cast<std::uintptr_t>(p);
        std::uintptr_t hash = ptr >> log<std::max(sizeof(T), alignof(T))>;
        return static_cast<std::size_t>(hash);
    }
};

template<class ValueType>
class AlignedStorage {
public:
    using reference = ValueType &;
    using pointer = ValueType *;
    using const_pointer = const ValueType *;

public:
    AlignedStorage() = default;

    template<class... Args>
    AlignedStorage(Args &&...args) noexcept(std::is_nothrow_constructible_v<ValueType, Args...>) {
        emplace(std::forward<Args>(args)...);
    }

public:
    template<class... Args>
    void emplace(Args &&...args) noexcept(std::is_nothrow_constructible_v<ValueType, Args...>) {
        ::new (data_) ValueType(std::forward<Args>(args)...);
    }

    void destroy() noexcept {
        reinterpret_cast<ValueType *>(data_)->~ValueType();
    }

    reference operator*() noexcept {
        return *reinterpret_cast<pointer>(data_);
    }

    pointer operator->() const noexcept {
        return const_cast<pointer>(reinterpret_cast<const_pointer>(data_));
    }

    void *get_ptr() const noexcept {
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

    pointer release() noexcept {
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
        : ptr_(ptr)
        , deleter_(deleter) {}

    ~DeleterGuard() {
        if (ptr_) {
            deleter_(ptr_);
        }
    }

    Ptr release() noexcept {
        return std::exchange(ptr_, Ptr{});
    }

private:
    Ptr ptr_;
    Deleter &deleter_;
};

}// namespace detail
}// namespace lu

#endif
