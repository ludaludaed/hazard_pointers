#ifndef __FREE_LIST__
#define __FREE_LIST__

#include <lu/intrusive/detail/compressed_tuple.h>
#include <lu/intrusive/detail/generic_hook.h>
#include <lu/intrusive/detail/get_traits.h>
#include <lu/intrusive/detail/node_holder.h>
#include <lu/intrusive/detail/pack_options.h>

#include <atomic>
#include <cassert>
#include <memory>
#include <thread>
#include <type_traits>


namespace lu {
namespace detail {

template<class VoidPointer>
class FreeListNode {
    template<class>
    friend struct FreeListNodeTraits;

    using pointer = std::pointer_traits<VoidPointer>::template rebind<FreeListNode>;
    using const_pointer = std::pointer_traits<VoidPointer>::template rebind<const FreeListNode>;

    pointer next;
};

template<class VoidPointer>
struct FreeListNodeTraits {
    using node = FreeListNode<VoidPointer>;
    using node_ptr = typename node::pointer;
    using const_node_ptr = typename node::const_pointer;

    static void set_next(node_ptr this_node, node_ptr next) {
        this_node->next = next;
    }

    static node_ptr get_next(const_node_ptr this_node) {
        return this_node->next;
    }
};

template<class ValueTraits>
class FreeList {
public:
    using value_traits = ValueTraits;
    using node_traits = typename value_traits::node_traits;

    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using const_node_ptr = typename node_traits::const_node_ptr;

    using pointer = typename value_traits::pointer;
    using const_pointer = typename value_traits::const_pointer;
    using reference = typename value_traits::reference;
    using const_reference = typename value_traits::const_reference;

public:
    explicit FreeList(const value_traits &value_traits = {}) noexcept
        : value_traits_(value_traits) {}

    FreeList(const FreeList &) = delete;

    FreeList(FreeList &&) = delete;

    void push(reference value) {
        std::thread::id current_id = std::this_thread::get_id();
        node_ptr new_node = value_traits_.to_node_ptr(value);
        if (current_id == owner_id_) {
            push_to_local(new_node);
        } else {
            push_to_global(new_node);
        }
    }

    void push_to_local(reference value) {
        node_ptr new_node = value_traits_.to_node_ptr(value);
        push_to_local(new_node);
    }

    void push_to_global(reference value) {
        node_ptr new_node = value_traits_.to_node_ptr(value);
        push_to_global(new_node);
    }

    pointer pop() noexcept {
        assert(owner_id_ == std::this_thread::get_id());
        if (!local_head) {
            local_head = global_head.exchange(nullptr, std::memory_order_acquire);
        }
        node_ptr result = pop_from_local();
        if (!result) {
            return nullptr;
        }
        return value_traits_.to_value_ptr(result);
    }

    bool empty() const noexcept {
        assert(owner_id_ == std::this_thread::get_id());
        if (local_head) {
            return true;
        } else {
            return global_head.load(std::memory_order_relaxed);
        }
    }

    void set_owner() noexcept {
        owner_id_ = std::this_thread::get_id();
    }

    void clear_owner() noexcept {
        owner_id_ = std::thread::id();
    }

private:
    void push_to_global(node_ptr new_node) noexcept {
        node_ptr current = global_head.load(std::memory_order_relaxed);
        do {
            node_traits::set_next(new_node, current);
        } while (!global_head.compare_exchange_weak(current, new_node, std::memory_order_release,
                                                    std::memory_order_relaxed));
    }

    void push_to_local(node_ptr new_node) noexcept {
        node_traits::set_next(new_node, local_head);
        local_head = new_node;
    }

    node_ptr pop_from_local() noexcept {
        node_ptr result = local_head;
        if (result) {
            local_head = node_traits::get_next(result);
        }
        return result;
    }

private:
    std::atomic<node_ptr> global_head{};
    node_ptr local_head{};
    std::thread::id owner_id_{};
    value_traits value_traits_;
};

template<class VoidPointer, class Tag>
struct FreeListHook : public NodeHolder<FreeListNode<VoidPointer>, Tag> {
    using hook_tags = HookTags<FreeListNodeTraits<VoidPointer>, Tag, false>;
};

template<class HookType>
struct FreeListDefaultHook {
    using free_list_default_hook = HookType;
};

template<class VoidPointer, class Tag>
struct FreeListBaseHook : public FreeListHook<VoidPointer, Tag>,
                          std::conditional_t<std::is_same_v<Tag, DefaultHookTag>,
                                             FreeListDefaultHook<FreeListHook<VoidPointer, Tag>>, NotDefaultHook> {};

struct DefaultFreeListHook {
    template<class ValueType>
    struct GetDefaultHook {
        using type = typename ValueType::free_list_default_hook;
    };

    struct is_default_hook_tag;
};

struct FreeListDefaults {
    using proto_value_traits = DefaultFreeListHook;
};

struct FreeListHookDefaults {
    using void_pointer = void *;
    using tag = DefaultHookTag;
};

}// namespace detail
}// namespace lu

namespace lu {
namespace detail {

template<class... Options>
struct make_free_list_base_hook {
    using pack_options = typename GetPackOptions<FreeListHookDefaults, Options...>::type;

    using void_pointer = typename pack_options::void_pointer;
    using tag = typename pack_options::tag;

    using type = FreeListBaseHook<void_pointer, tag>;
};

template<class ValueType, class... Options>
struct make_free_list {
    using pack_options = typename GetPackOptions<FreeListDefaults, Options...>::type;
    using value_traits = typename GetValueTraits<ValueType, typename pack_options::proto_value_traits>::type;

    using type = FreeList<value_traits>;
};

}// namespace detail

template<class... Options>
using free_list_base_hook = typename detail::make_free_list_base_hook<Options...>::type;

template<class ValueType, class... Options>
using free_list = typename detail::make_free_list<ValueType, Options...>::type;

}// namespace lu
#endif
