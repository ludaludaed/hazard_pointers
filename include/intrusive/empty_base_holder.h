#ifndef __INTRUSIVE_EMPTY_BASE_HOLDER_H__
#define __INTRUSIVE_EMPTY_BASE_HOLDER_H__

#include <type_traits>


namespace lu {
    namespace detail {
        class default_base_tag {};

        template<class ValueType>
        constexpr bool is_empty_base = std::is_empty_v<ValueType> && !std::is_final_v<ValueType>;

        template<class ValueType, class Tag = default_base_tag, bool EmptyBase = is_empty_base<ValueType>>
        class EmptyBaseHolder;

        template<class ValueType, class Tag>
        class EmptyBaseHolder<ValueType, Tag, false> {
        public:
            using type = ValueType;

        public:
            template<class... Args, class = std::enable_if_t<std::is_constructible_v<ValueType, Args...>>>
            explicit EmptyBaseHolder(Args &&...args) noexcept(std::is_nothrow_constructible_v<ValueType, Args...>)
                : value_(std::forward<Args>(args)...) {}

            EmptyBaseHolder(const EmptyBaseHolder &other) noexcept(std::is_nothrow_copy_constructible_v<ValueType>)
                : value_(other.value_) {};

            EmptyBaseHolder(EmptyBaseHolder &&other) noexcept(std::is_nothrow_move_constructible_v<ValueType>)
                : value_(std::move(other.value_)) {};

            EmptyBaseHolder &operator=(const EmptyBaseHolder &other) {
                value_ = other.value_;
                return *this;
            }

            EmptyBaseHolder &operator=(EmptyBaseHolder &&other) noexcept(std::is_nothrow_move_assignable_v<ValueType>) {
                value_ = std::move(other.value_);
                return *this;
            }

        public:
            ValueType &get() noexcept {
                return value_;
            }

            const ValueType &get() const noexcept {
                return value_;
            }

        private:
            ValueType value_;
        };

        template<class ValueType, class Tag>
        class EmptyBaseHolder<ValueType, Tag, true> : public ValueType {
        public:
            using type = ValueType;

        public:
            template<class... Args, class = std::enable_if_t<std::is_constructible_v<ValueType, Args...>>>
            explicit EmptyBaseHolder(Args &&...args) noexcept(std::is_nothrow_constructible_v<ValueType, Args...>)
                : ValueType(std::forward<Args>(args)...) {}

            EmptyBaseHolder(const EmptyBaseHolder &other) noexcept(std::is_nothrow_copy_constructible_v<ValueType>)
                : ValueType(other.get()) {};

            EmptyBaseHolder(EmptyBaseHolder &&other) noexcept(std::is_nothrow_move_constructible_v<ValueType>)
                : ValueType(std::move(other.get())) {};

            EmptyBaseHolder &operator=(const EmptyBaseHolder &other) {
                ValueType::operator=(other.get());
                return *this;
            }

            EmptyBaseHolder &operator=(EmptyBaseHolder &&other) noexcept(std::is_nothrow_move_assignable_v<ValueType>) {
                ValueType::operator=(std::move(other.get()));
                return *this;
            }

        public:
            ValueType &get() noexcept {
                return *this;
            }

            const ValueType &get() const noexcept {
                return *this;
            }
        };
    }// namespace detail
}// namespace lu

#endif