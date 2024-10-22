#ifndef __INTRUSIVE_SIZE_TRAITS_H__
#define __INTRUSIVE_SIZE_TRAITS_H__

namespace lu {
    namespace detail {
        template<class SizeType, bool IsConstSize>
        class SizeTraits;

        template<class SizeType>
        class SizeTraits<SizeType, true> {
        public:
            using size_type = SizeType;
            static constexpr bool is_const_size = true;

        public:
            inline void increase(size_type n) { size_ += n; }

            inline void decrease(size_type n) { size_ -= n; }

            inline void increment() { ++size_; }

            inline void decrement() { --size_; }

            inline size_type get_size() const { return size_; }

            inline void set_size(size_type size) { size_ = size; }

        private:
            size_type size_{};
        };

        template<class SizeType>
        class SizeTraits<SizeType, false> {
        public:
            using size_type = SizeType;
            static constexpr bool is_const_size = false;

        public:
            inline void increase(size_type) {}

            inline void decrease(size_type) {}

            inline void increment() {}

            inline void decrement() {}

            inline size_type get_size() const {
                return size_type{};
            }

            inline void set_size(size_type) {}
        };
    }// namespace detail
}// namespace lu

#endif