#ifndef __UTILS_H__
#define __UTILS_H__

#include <lu/intrusive/detail/utils.h>

#include <memory>
#include <type_traits>
#include <utility>


#define CACHE_LINE_ALIGNAS alignas(64)

namespace lu {
namespace detail {

template <std::size_t argument, std::size_t base = 2, bool = (argument < base)>
static constexpr std::size_t log = 1 + log<argument / base, base>;

template <std::size_t argument, std::size_t base>
static constexpr std::size_t log<argument, base, true> = 0;

struct FastPointerHash {
    template <class T>
    std::size_t operator()(T *p) const noexcept {
        std::size_t hash = reinterpret_cast<std::size_t>(p);
        if constexpr (!std::is_void_v<T>) {
            hash >>= log<alignof(T)>;
        }
        return static_cast<std::size_t>(hash);
    }
};

template <class Allocator>
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

    pointer release() noexcept { return std::exchange(ptr_, pointer{}); }

private:
    Allocator &allocator_;
    pointer ptr_{};
};

template <class Ptr, class Deleter>
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

    Ptr release() noexcept { return std::exchange(ptr_, Ptr{}); }

private:
    Ptr ptr_;
    Deleter &deleter_;
};

}// namespace detail
}// namespace lu

#endif
