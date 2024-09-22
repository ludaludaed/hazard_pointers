#ifndef __INTRUSIVE_UNORDERED_SET_H__
#define __INTRUSIVE_UNORDERED_SET_H__

#include "get_traits.h"
#include "hashtable.h"
#include "options.h"
#include "pack_options.h"


namespace lu {
    template<class... Options>
    struct make_unordered_set_base_hook {
        using pack_options = typename get_pack_options<HashtableHookDefaults, Options...>::type;

        using type = HashtableBaseHook<typename pack_options::void_pointer,
                                       typename pack_options::tag,
                                       pack_options::store_hash,
                                       pack_options::is_auto_unlink>;
    };

    template<class... Options>
    using unordered_set_base_hook = typename make_unordered_set_base_hook<Options...>::type;

    template<class ValueType, class... Options>
    struct make_unordered_set {
        using pack_options = typename get_pack_options<HashtableDefaults, Options...>::type;

        using value_traits = typename detail::get_value_traits<ValueType, typename pack_options::proto_value_traits>::type;
        using bucket_traits = typename detail::get_bucket_traits<value_traits, typename pack_options::size_type, typename pack_options::proto_bucket_traits>::type;

        using key_of_value = typename get_key_of_value<typename pack_options::key_of_value, ValueType>::type;
        using key_type = typename key_of_value::type;
        using hash = typename get_hash<typename pack_options::hash, key_type>::type;
        using equal = typename get_equal_to<typename pack_options::equal, key_type>::type;

        using size_type = typename pack_options::size_type;

        using type = IntrusiveHashtable<value_traits, bucket_traits, key_of_value, hash, equal, size_type, false>;
    };

    template<class ValueType, class... Options>
    using unordered_set = typename make_unordered_set<ValueType, Options...>::type;

    template<class ValueType, class... Options>
    struct make_unordered_multiset {
        using pack_options = typename get_pack_options<HashtableDefaults, Options...>::type;

        using value_traits = typename detail::get_value_traits<ValueType, typename pack_options::proto_value_traits>::type;
        using bucket_traits = typename detail::get_bucket_traits<value_traits, typename pack_options::size_type, typename pack_options::proto_bucket_traits>::type;

        using key_of_value = typename get_key_of_value<typename pack_options::key_of_value, ValueType>::type;
        using key_type = typename key_of_value::type;
        using hash = typename get_hash<typename pack_options::hash, key_type>::type;
        using equal = typename get_equal_to<typename pack_options::equal, key_type>::type;

        using size_type = typename pack_options::size_type;

        using type = IntrusiveHashtable<value_traits, bucket_traits, key_of_value, hash, equal, size_type, true>;
    };

    template<class ValueType, class... Options>
    using unordered_multiset = typename make_unordered_set<ValueType, Options...>::type;
}// namespace lu

#endif