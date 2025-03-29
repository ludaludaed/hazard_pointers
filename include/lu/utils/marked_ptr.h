#ifndef __MARKED_PTR_H__
#define __MARKED_PTR_H__

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>


namespace lu {

template<class ValueType>
class marked_ptr {
public:
    using element_type = ValueType;

public:
    marked_ptr() noexcept = default;

    marked_ptr(std::nullptr_t) noexcept {}

    template<class _ValueType, class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
    marked_ptr(_ValueType *other) noexcept
        : ptr_(make_marked_ptr(other, false)) {}

    template<class _ValueType, class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
    marked_ptr(_ValueType *other, bool marked) noexcept
        : ptr_(make_marked_ptr(other, marked)) {}

    template<class _ValueType, class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
    marked_ptr(const marked_ptr<_ValueType> &other, bool marked) noexcept
        : ptr_(make_marked_ptr(other.get(), marked)) {}

    marked_ptr(const marked_ptr &other) = default;

    template<class _ValueType, class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
    marked_ptr(const marked_ptr<_ValueType> &other) noexcept
        : ptr_(make_marked_ptr(other.get(), other.is_marked())) {}

    std::add_lvalue_reference_t<element_type> operator*() const noexcept {
        return *get();
    }

    element_type *operator->() const noexcept {
        return get();
    }

    operator element_type *() const noexcept {
        return get();
    }

    explicit operator bool() const noexcept {
        return get();
    }

    element_type *raw() const noexcept {
        return reinterpret_cast<element_type *>(ptr_);
    }

    element_type *get() const noexcept {
        return reinterpret_cast<element_type *>(ptr_ & ~(1));
    }

    void set_mark(bool value) noexcept {
        ptr_ = make_marked_ptr(get(), value);
    }

    void mark() noexcept {
        ptr_ |= 1;
    }

    void unmark() noexcept {
        ptr_ &= ~1;
    }

    bool is_marked() const noexcept {
        return ptr_ & 1;
    }

    void swap(marked_ptr &other) noexcept {
        std::swap(ptr_, other.ptr_);
    }

    friend void swap(marked_ptr &left, marked_ptr &right) noexcept {
        left.swap(right);
    }

    friend bool operator==(const marked_ptr &left, const marked_ptr &right) noexcept {
        return left.ptr_ == right.ptr_;
    }

    friend bool operator!=(const marked_ptr &left, const marked_ptr &right) noexcept {
        return !(left == right);
    }

    friend bool operator<(const marked_ptr &left, const marked_ptr &right) noexcept {
        return left.ptr_ < right.ptr_;
    }

    friend bool operator>(const marked_ptr &left, const marked_ptr &right) noexcept {
        return right < left;
    }

    friend bool operator<=(const marked_ptr &left, const marked_ptr &right) noexcept {
        return !(right < left);
    }

    friend bool operator>=(const marked_ptr &left, const marked_ptr &right) noexcept {
        return !(left < right);
    }

    template<class _ValueType, class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
    static marked_ptr pointer_to(_ValueType &value) noexcept {
        return marked_ptr(&value);
    }

    template<class _ValueType>
    static marked_ptr static_cast_from(const marked_ptr<_ValueType> &from) noexcept {
        return marked_ptr(static_cast<ValueType *>(from.get()), from.is_marked());
    }

    template<class _ValueType>
    static marked_ptr const_cast_from(const marked_ptr<_ValueType> &from) noexcept {
        return marked_ptr(const_cast<ValueType *>(from.get()), from.is_marked());
    }

    template<class _ValueType>
    static marked_ptr dynamic_cast_from(const marked_ptr<_ValueType> &from) noexcept {
        return marked_ptr(dynamic_cast<ValueType *>(from.get()), from.is_marked());
    }

private:
    static inline std::uintptr_t make_marked_ptr(element_type *raw_ptr, bool marked) noexcept {
        static constexpr std::uintptr_t mask = 1;
        std::uintptr_t ptr = reinterpret_cast<std::uintptr_t>(raw_ptr);
        return ptr | (mask & -std::uintptr_t(marked));
    }

private:
    std::uintptr_t ptr_{};
};

}// namespace lu

#endif
