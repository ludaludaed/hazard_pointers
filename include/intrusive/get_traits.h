#ifndef __INTRUSIVE_GET_TRAITS_H__
#define __INTRUSIVE_GET_TRAITS_H__

namespace lu {
namespace detail {

template<class ValueType, class ProtoValueTraits>
struct GetValueTraits {
    using type = typename ProtoValueTraits::template Apply<ValueType>::type;
};

template<class ValueTraits, class SizeType, class ProtoBucketTraits>
struct GetBucketTraits {
    using type = typename ProtoBucketTraits::template Apply<ValueTraits, SizeType>::type;
};

}// namespace detail
}// namespace lu

#endif