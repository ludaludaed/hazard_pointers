#ifndef __INTRUSIVE_UNORDERED_SET_H__
#define __INTRUSIVE_UNORDERED_SET_H__

#include "get_traits.h"
#include "hashtable.h"
#include "pack_options.h"


namespace lu {
namespace detail {

template<class... Options>
struct make_unordered_set_base_hook {
    using pack_options = typename GetPackOptions<HashtableHookDefaults, Options...>::type;

    using type = HashtableBaseHook<typename pack_options::void_pointer, typename pack_options::tag,
                                   pack_options::store_hash, pack_options::is_auto_unlink>;
};

template<class ValueType, class... Options>
struct make_unordered_set {
    using pack_options = typename GetPackOptions<HashtableDefaults, Options...>::type;

    using value_traits = typename GetValueTraits<ValueType, typename pack_options::proto_value_traits>::type;
    using bucket_traits = typename GetBucketTraits<value_traits, typename pack_options::size_type,
                                                           typename pack_options::proto_bucket_traits>::type;

    using key_of_value = typename GetKeyOfValue<typename pack_options::key_of_value, ValueType>::type;
    using key_type = typename key_of_value::type;
    using hash = typename GetHash<typename pack_options::hash, key_type>::type;
    using equal = typename GetEqualTo<typename pack_options::equal, key_type>::type;

    using size_type = typename pack_options::size_type;
    using flags = HashtableFlags<pack_options::is_power_2_buckets, false>;

    using type = IntrusiveHashtable<value_traits, bucket_traits, key_of_value, hash, equal, size_type, flags>;
};

template<class ValueType, class... Options>
struct make_unordered_multiset {
    using pack_options = typename GetPackOptions<HashtableDefaults, Options...>::type;

    using value_traits = typename GetValueTraits<ValueType, typename pack_options::proto_value_traits>::type;
    using bucket_traits = typename GetBucketTraits<value_traits, typename pack_options::size_type,
                                                           typename pack_options::proto_bucket_traits>::type;

    using key_of_value = typename GetKeyOfValue<typename pack_options::key_of_value, ValueType>::type;
    using key_type = typename key_of_value::type;
    using hash = typename GetHash<typename pack_options::hash, key_type>::type;
    using equal = typename GetEqualTo<typename pack_options::equal, key_type>::type;

    using size_type = typename pack_options::size_type;
    using flags = HashtableFlags<pack_options::is_power_2_buckets, true>;

    using type = IntrusiveHashtable<value_traits, bucket_traits, key_of_value, hash, equal, size_type, flags>;
};

}// namespace detail

template<class... Options>
using unordered_set_base_hook = typename detail::make_unordered_set_base_hook<Options...>::type;

template<class ValueType, class... Options>
using unordered_set = typename detail::make_unordered_set<ValueType, Options...>::type;

template<class ValueType, class... Options>
using unordered_multiset = typename detail::make_unordered_multiset<ValueType, Options...>::type;

}// namespace lu

#endif