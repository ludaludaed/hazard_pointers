#ifndef __SHARED_FREE_LIST_H__
#define __SHARED_FREE_LIST_H__

#include <lu/intrusive/detail/compressed_tuple.h>
#include <lu/intrusive/detail/generic_hook.h>
#include <lu/intrusive/detail/get_traits.h>
#include <lu/intrusive/detail/node_holder.h>
#include <lu/intrusive/detail/pack_options.h>

#include <atomic>
#include <cassert>
#include <memory>
#include <type_traits>


namespace lu {
namespace detail {

template <class VoidPointer>
class SharedFreeListNode {
    template <class>
    friend struct SharedFreeListNodeTraits;

    using pointer = std::pointer_traits<VoidPointer>::template rebind<SharedFreeListNode>;
    using const_pointer = std::pointer_traits<VoidPointer>::template rebind<const SharedFreeListNode>;

    pointer next{};
};

template <class VoidPointer>
struct SharedFreeListNodeTraits {
    using node = SharedFreeListNode<VoidPointer>;
    using node_ptr = typename node::pointer;
    using const_node_ptr = typename node::const_pointer;

    static void set_next(node_ptr this_node, node_ptr next) noexcept { this_node->next = next; }

    static node_ptr get_next(const_node_ptr this_node) noexcept { return this_node->next; }
};

template <class ValueTraits>
class SharedFreeList : private ValueTraits {
public:
    using value_traits = ValueTraits;
    using node_traits = typename value_traits::node_traits;

    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using const_node_ptr = typename node_traits::const_node_ptr;

    using value_type = typename value_traits::value_type;

    using pointer = typename value_traits::pointer;
    using const_pointer = typename value_traits::const_pointer;
    using difference_type = typename value_traits::difference_type;

    using reference = typename value_traits::reference;
    using const_reference = typename value_traits::const_reference;

public:
    explicit SharedFreeList(const value_traits &value_traits = {}) noexcept
        : ValueTraits(value_traits) {}

    SharedFreeList(const SharedFreeList &) = delete;

    SharedFreeList(SharedFreeList &&) = delete;

    void push_to_local(reference value) noexcept {
        node_ptr new_node = ValueTraits::to_node_ptr(value);
        node_traits::set_next(new_node, local_head_);
        local_head_ = new_node;
    }

    void push_to_global(reference value) noexcept {
        node_ptr new_node = ValueTraits::to_node_ptr(value);
        node_ptr head = global_head_.load(std::memory_order_relaxed);
        do {
            node_traits::set_next(new_node, head);
        } while (!global_head_.compare_exchange_weak(head, new_node, std::memory_order_release,
                                                     std::memory_order_relaxed));
    }

    pointer pop() noexcept {
        if (!local_head_) {
            local_head_ = global_head_.exchange(nullptr, std::memory_order_acquire);
        }
        if (local_head_) {
            node_ptr result = local_head_;
            local_head_ = node_traits::get_next(result);
            return ValueTraits::to_value_ptr(result);
        }
        return nullptr;
    }

    bool empty() const noexcept {
        if (local_head_) {
            return true;
        } else {
            return global_head_.load(std::memory_order_relaxed);
        }
    }

private:
    std::atomic<node_ptr> global_head_{};
    node_ptr local_head_{};
};

template <class VoidPointer, class Tag>
struct SharedFreeListHook : public NodeHolder<SharedFreeListNode<VoidPointer>, Tag> {
    using hook_tags = HookTags<SharedFreeListNodeTraits<VoidPointer>, Tag, false>;
};

template <class HookType>
struct SharedFreeListDefaultHook {
    using free_list_default_hook = HookType;
};

template <class VoidPointer, class Tag>
struct SharedFreeListBaseHook
    : public SharedFreeListHook<VoidPointer, Tag>,
      std::conditional_t<std::is_same_v<Tag, DefaultHookTag>,
                         SharedFreeListDefaultHook<SharedFreeListHook<VoidPointer, Tag>>, NotDefaultHook> {};

struct DefaultSharedFreeListHook : public UseDefaultHookTag {
    template <class ValueType>
    struct GetDefaultHook {
        using type = typename ValueType::free_list_default_hook;
    };
};

struct SharedFreeListDefaults {
    using proto_value_traits = DefaultSharedFreeListHook;
};

struct SharedFreeListHookDefaults {
    using void_pointer = void *;
    using tag = DefaultHookTag;
};

}// namespace detail
}// namespace lu

namespace lu {
namespace detail {

template <class... Options>
struct make_shared_free_list_base_hook {
    using pack_options = typename GetPackOptions<SharedFreeListHookDefaults, Options...>::type;

    using void_pointer = typename pack_options::void_pointer;
    using tag = typename pack_options::tag;

    using type = SharedFreeListBaseHook<void_pointer, tag>;
};

template <class ValueType, class... Options>
struct make_shared_free_list {
    using pack_options = typename GetPackOptions<SharedFreeListDefaults, Options...>::type;
    using value_traits = typename GetValueTraits<ValueType, typename pack_options::proto_value_traits>::type;

    using type = SharedFreeList<value_traits>;
};

}// namespace detail

template <class... Options>
using shared_free_list_base_hook = typename detail::make_shared_free_list_base_hook<Options...>::type;

template <class ValueType, class... Options>
using shared_free_list = typename detail::make_shared_free_list<ValueType, Options...>::type;

}// namespace lu
#endif
