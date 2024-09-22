#ifndef __INTRUSIVE_BASE_VALUE_TRAITS_H__
#define __INTRUSIVE_BASE_VALUE_TRAITS_H__

#include "node_holder.h"
#include "utils.h"
#include <memory>


namespace lu {
    template<class ValueType, class NodeTraits, class Tag, bool IsAutoUnlink>
    struct BaseValueTraits {
        using node_traits = NodeTraits;

        using node = typename NodeTraits::node;
        using node_ptr = typename node_traits::node_ptr;
        using const_node_ptr = typename node_traits::const_node_ptr;

        using node_reference = node &;
        using const_node_reference = const node &;

        using value_type = ValueType;
        using pointer = typename std::pointer_traits<node_ptr>::template rebind<value_type>;
        using const_pointer = typename std::pointer_traits<const_node_ptr>::template rebind<const value_type>;

        using reference = value_type &;
        using const_reference = const value_type &;

        using node_holder = NodeHolder<node, Tag>;

        using node_holder_ptr = typename std::pointer_traits<node_ptr>::template rebind<node_holder>;
        using const_node_holder_ptr = typename std::pointer_traits<const_node_ptr>::template rebind<const node_holder>;

        using node_holder_reference = node_holder &;
        using const_node_holder_reference = const node_holder &;

        static constexpr bool is_auto_unlink = IsAutoUnlink;

    public:
        node_ptr to_node_ptr(reference value) const noexcept {
            return std::pointer_traits<node_ptr>::pointer_to(
                    static_cast<node_reference>(static_cast<node_holder_reference>(value)));
        }

        const_node_ptr to_node_ptr(const_reference value) const noexcept {
            return std::pointer_traits<const_node_ptr>::pointer_to(
                    static_cast<const_node_reference>(static_cast<const_node_holder_reference>(value)));
        }

        pointer to_value_ptr(node_ptr node) const noexcept {
            return pointer_cast_traits<pointer>::static_cast_from(pointer_cast_traits<node_holder_ptr>::static_cast_from(node));
        }

        const_pointer to_value_ptr(const_node_ptr node) const noexcept {
            return pointer_cast_traits<const_pointer>::static_cast_from(pointer_cast_traits<const_node_holder_ptr>::static_cast_from(node));
        }
    };

    template<class ValueType, class HookType>
    struct hook_to_value_traits {
        using tags = typename HookType::hook_tags;
        using type = BaseValueTraits<ValueType, typename tags::node_traits, typename tags::tag, tags::is_auto_unlink>;
    };
}// namespace lu

#endif