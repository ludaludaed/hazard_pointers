#ifndef __INTRUSIVE_GENERIC_HOOK__
#define __INTRUSIVE_GENERIC_HOOK__

#include "node_holder.h"

#include <memory>


namespace lu {
namespace detail {

class DefaultHookTag {};

class NotDefaultHook {};

template<class NodeTraits, class Tag, bool IsAutoUnlink>
struct HookTags {
    using node_traits = NodeTraits;
    using tag = Tag;
    static constexpr bool is_auto_unlink = IsAutoUnlink;
};

template<class NodeAlgo, class NodeTraits, class Tag, bool IsAutoUnlink>
class GenericHook : public NodeHolder<typename NodeTraits::node, Tag> {
public:
    using node_traits = NodeTraits;

    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using const_node_ptr = typename node_traits::const_node_ptr;

    static constexpr bool is_auto_unlink = IsAutoUnlink;

    using hook_tags = HookTags<NodeTraits, Tag, IsAutoUnlink>;

public:
    GenericHook() noexcept {
        NodeAlgo::init(as_node_ptr());
    }

    GenericHook(const GenericHook &) noexcept {
        NodeAlgo::init(as_node_ptr());
    }

    GenericHook &operator=(const GenericHook &) noexcept {
        return *this;
    }

    ~GenericHook() {
        if constexpr (IsAutoUnlink) {
            unlink();
        }
    }

    node_ptr as_node_ptr() noexcept {
        return std::pointer_traits<node_ptr>::pointer_to(static_cast<node &>(*this));
    }

    const_node_ptr as_node_ptr() const noexcept {
        return std::pointer_traits<const_node_ptr>::pointer_to(static_cast<const node &>(*this));
    }

    bool is_linked() const {
        return !NodeAlgo::unique(as_node_ptr());
    }

    bool unique() const noexcept {
        return NodeAlgo::unique(as_node_ptr());
    }

    void unlink() noexcept {
        node_ptr this_ptr = as_node_ptr();
        if (!NodeAlgo::unique(this_ptr)) {
            NodeAlgo::unlink(this_ptr);
            NodeAlgo::init(this_ptr);
        }
    }
};

}// namespace detail
}// namespace lu


#endif