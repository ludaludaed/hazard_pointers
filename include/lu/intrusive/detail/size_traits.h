#ifndef __INTRUSIVE_SIZE_TRAITS_H__
#define __INTRUSIVE_SIZE_TRAITS_H__

namespace lu {
namespace detail {

template <class SizeType, bool TrackSize>
class SizeTraits;

template <class SizeType>
class SizeTraits<SizeType, true> {
public:
    using size_type = SizeType;
    static constexpr bool is_tracking_size = true;

public:
    inline void increase(size_type n) noexcept { size_ += n; }

    inline void decrease(size_type n) noexcept { size_ -= n; }

    inline void increment() noexcept { ++size_; }

    inline void decrement() noexcept { --size_; }

    inline size_type get_size() const noexcept { return size_; }

    inline void set_size(size_type size) noexcept { size_ = size; }

private:
    size_type size_{};
};

template <class SizeType>
class SizeTraits<SizeType, false> {
public:
    using size_type = SizeType;
    static constexpr bool is_tracking_size = false;

public:
    inline void increase(size_type) noexcept {}

    inline void decrease(size_type) noexcept {}

    inline void increment() noexcept {}

    inline void decrement() noexcept {}

    inline size_type get_size() const noexcept { return size_type{}; }

    inline void set_size(size_type) {}
};

}// namespace detail
}// namespace lu

#endif
