#ifndef __INTRUSIVE_UTILS_H__
#define __INTRUSIVE_UTILS_H__

#include <array>
#include <memory>
#include <type_traits>


#define HAS_MEMBER_FUNC(name, func)                                                                                    \
    template<class Type, class Signature>                                                                              \
    class name {                                                                                                       \
        using yes = int;                                                                                               \
        using no = char;                                                                                               \
                                                                                                                       \
        template<class T, T>                                                                                           \
        struct check_type;                                                                                             \
                                                                                                                       \
        template<class>                                                                                                \
        static no test(...);                                                                                           \
                                                                                                                       \
        template<class T>                                                                                              \
        static yes test(check_type<Signature, &T::func> *);                                                            \
                                                                                                                       \
    public:                                                                                                            \
        static const bool value = sizeof(test<Type>(0)) == sizeof(yes);                                                \
    };


namespace lu {

template<class Ptr>
struct get_void_ptr {
    using type = typename std::pointer_traits<Ptr>::template rebind<void>;
};

template<class Ptr>
using get_void_ptr_t = typename get_void_ptr<Ptr>::type;

template<bool Value>
struct get_bool_type;

template<>
struct get_bool_type<true> {
    using type = std::true_type;
};

template<>
struct get_bool_type<false> {
    using type = std::false_type;
};

template<bool Value>
using get_bool_t = typename get_bool_type<Value>::type;

using yes_type = std::array<uint8_t, 2>;
using no_type = std::array<uint8_t, 1>;

template<class ValueType>
ValueType *to_raw_pointer(ValueType *ptr) {
    return ptr;
}

template<class Pointer>
typename std::pointer_traits<Pointer>::element_type *to_raw_pointer(const Pointer &ptr) {
    return to_raw_pointer(ptr.operator->());
}

HAS_MEMBER_FUNC(has_static_cast_from, static_cast_from)
HAS_MEMBER_FUNC(has_const_cast_from, const_cast_from)
HAS_MEMBER_FUNC(has_dynamic_cast_from, dynamic_cast_from)

template<class TPtr>
struct pointer_cast_traits {
    using pointer = TPtr;
    using pointer_traits = std::pointer_traits<pointer>;
    using element_type = typename pointer_traits::element_type;

    template<class UPtr>
    static pointer static_cast_from(const UPtr &uptr) {
        static const bool has_cast = has_static_cast_from<pointer, pointer (*)(UPtr)>::value
                                     || has_static_cast_from<pointer, pointer (*)(const UPtr &)>::value;
        return static_cast_from(uptr, get_bool_t<has_cast>());
    }

    template<class UPtr>
    static pointer const_cast_from(const UPtr &uptr) {
        static const bool has_cast = has_const_cast_from<pointer, pointer (*)(UPtr)>::value
                                     || has_const_cast_from<pointer, pointer (*)(const UPtr &)>::value;
        return const_cast_from(uptr, get_bool_t<has_cast>());
    }

    template<class UPtr>
    static pointer dynamic_cast_from(const UPtr &uptr) {
        static const bool has_cast = has_dynamic_cast_from<pointer, pointer (*)(UPtr)>::value
                                     || has_dynamic_cast_from<pointer, pointer (*)(const UPtr &)>::value;
        return dynamic_cast_from(uptr, get_bool_t<has_cast>());
    }

private:
    // static_cast
    template<class UPtr>
    static pointer static_cast_from(const UPtr &uptr, get_bool_t<true>) {
        return pointer::static_cast_from(uptr);
    }

    template<class UPtr>
    static pointer static_cast_from(const UPtr &uptr, get_bool_t<false>) {
        if (uptr) {
            return pointer_traits::pointer_to(static_cast<element_type &>(*uptr));
        }
        return pointer{};
    }

    //const_cast
    template<class UPtr>
    static pointer const_cast_from(const UPtr &uptr, get_bool_t<true>) {
        return pointer::const_cast_from(uptr);
    }

    template<class UPtr>
    static pointer const_cast_from(const UPtr &uptr, get_bool_t<false>) {
        if (uptr) {
            return pointer_traits::pointer_to(const_cast<element_type &>(*uptr));
        }
        return pointer{};
    }

    //dynamic_cast
    template<class UPtr>
    static pointer dynamic_cast_from(const UPtr &uptr, get_bool_t<true>) {
        return pointer::dynamic_cast_from(uptr);
    }

    template<class UPtr>
    static pointer dynamic_cast_from(const UPtr &uptr, get_bool_t<false>) {
        if (uptr) {
            return pointer_traits::pointer_to(dynamic_cast<element_type &>(*uptr));
        }
        return pointer{};
    }
};

template<class T>
struct pointer_cast_traits<T *> {
    using pointer = T *;
    using pointer_traits = std::pointer_traits<pointer>;
    using element_type = typename pointer_traits::element_type;

    template<class UPtr>
    static pointer static_cast_from(UPtr *uptr) {
        return static_cast<pointer>(uptr);
    }

    template<class UPtr>
    static pointer const_cast_from(UPtr *uptr) {
        return const_cast<pointer>(uptr);
    }

    template<class UPtr>
    static pointer dynamic_cast_from(UPtr *uptr) {
        return dynamic_cast<pointer>(uptr);
    }
};

namespace detail {

template<class ConstPtr>
struct erase_const_types {
    using const_element_type = typename std::pointer_traits<ConstPtr>::element_type;
    using non_const_element_type = std::remove_const_t<const_element_type>;
    using non_const_pointer = typename std::pointer_traits<ConstPtr>::template rebind<non_const_element_type>;
};

template<class ConstPtr>
typename detail::erase_const_types<ConstPtr>::non_const_pointer erase_const(const ConstPtr &ptr) {
    using pointer_cast_traits = pointer_cast_traits<typename detail::erase_const_types<ConstPtr>::non_const_pointer>;
    return pointer_cast_traits::const_cast_from(ptr);
}

}// namespace detail
}// namespace lu

#endif