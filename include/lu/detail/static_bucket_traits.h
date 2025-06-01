#ifndef __STATIC_BUCKET_TRAITS_H__
#define __STATIC_BUCKET_TRAITS_H__

#include <array>

namespace lu {
namespace detail {

template <std::size_t Size, class BucketType>
class StaticBucketTraits {
    using Buckets = std::array<BucketType, Size>;

public:
    using bucket_ptr = Buckets::pointer;
    using size_type = std::size_t;

public:
    size_type size() const noexcept {
        return data_.size();
    }

    bucket_ptr data() const noexcept {
        return const_cast<bucket_ptr>(data_.data());
    }

    void swap(StaticBucketTraits &other) noexcept {
        std::swap(data_, other.data_);
    }

    friend void swap(StaticBucketTraits &left, StaticBucketTraits &right) noexcept {
        left.swap(right);
    }

private:
    Buckets data_{};
};

}// namespace detail
}// namespace lu

#endif
