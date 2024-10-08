#ifndef __INTRUSIVE_GET_TRAITS_H__
#define __INTRUSIVE_GET_TRAITS_H__

namespace lu {
    namespace detail {
        template<class ValueType, class ProtoValueTraits>
        struct get_value_traits {
            using type = typename ProtoValueTraits::template apply<ValueType>::type;
        };

        template<class ValueTraits, class SizeType, class ProtoBucketTraits>
        struct get_bucket_traits {
            using type = typename ProtoBucketTraits::template apply<ValueTraits, SizeType>::type;
        };
    }// namespace detail
}// namespace lu

#endif