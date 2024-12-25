#ifndef __MARKED_PTR_H__
#define __MARKED_PTR_H__

#include <cstdint>
#include <type_traits>
#include <utility>


namespace lu {
    template<class ValueType>
    class MarkedPointer {
    public:
        using element_type = ValueType;

    public:
        MarkedPointer() noexcept = default;

        template<class _ValueType, class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
        MarkedPointer(_ValueType *other) noexcept
            : ptr_(reinterpret_cast<uintptr_t>(other)) {}

        template<class _ValueType, class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
        MarkedPointer(_ValueType *other, bool bit_value) noexcept
            : ptr_(reinterpret_cast<uintptr_t>(other) | uintptr_t(bit_value)) {}

        template<class _ValueType, class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
        MarkedPointer(const MarkedPointer<_ValueType> &other, bool bit_value) noexcept
            : ptr_(reinterpret_cast<uintptr_t>(other.get()) | uintptr_t(bit_value)) {}

        MarkedPointer(const MarkedPointer &other) = default;

        template<class _ValueType, class = std::enable_if_t<std::is_convertible_v<_ValueType *, ValueType *>>>
        MarkedPointer(const MarkedPointer<_ValueType> &other) noexcept
            : ptr_(other.ptr_) {}

    public:
        element_type &operator*() const noexcept {
            return *get();
        }

        element_type *operator->() const noexcept {
            return get();
        }

        explicit operator bool() const noexcept {
            return get();
        }

        operator element_type *() const noexcept {
            return get();
        }

        element_type *all() const {
            return reinterpret_cast<element_type *>(ptr_);
        }

        element_type *get() const {
            return reinterpret_cast<element_type *>(ptr_ & ~(1));
        }

        void set_bit() noexcept {
            ptr_ |= 1;
        }

        void clear_bit() noexcept {
            ptr_ &= ~1;
        }

        bool get_bit() const noexcept {
            return ptr_ & 1;
        }

        static MarkedPointer pointer_to(element_type &value) {
            return MarkedPointer(&value);
        }

        void swap(MarkedPointer &other) noexcept {
            std::swap(ptr_, other.ptr_);
        }

        friend void swap(MarkedPointer &left, MarkedPointer &right) noexcept {
            left.swap(right);
        }

        friend bool operator==(const MarkedPointer &left, const MarkedPointer &right) {
            return left.ptr_ == right.ptr_;
        }

        friend bool operator!=(const MarkedPointer &left, const MarkedPointer &right) {
            return !(left == right);
        }

        friend bool operator<(const MarkedPointer &left, const MarkedPointer &right) {
            return left.ptr_ < right.ptr_;
        }

        friend bool operator>(const MarkedPointer &left, const MarkedPointer &right) {
            return right < left;
        }

        friend bool operator<=(const MarkedPointer &left, const MarkedPointer &right) {
            return !(right < left);
        }

        friend bool operator>=(const MarkedPointer &left, const MarkedPointer &right) {
            return !(left < right);
        }

    private:
        uintptr_t ptr_{};
    };

    template<class ValueType>
    using marked_ptr = MarkedPointer<ValueType>;
}// namespace lu

#endif