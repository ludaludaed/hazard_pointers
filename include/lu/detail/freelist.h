#ifndef __FREE_LIST__
#define __FREE_LIST__

#include <lu/intrusive/detail/compressed_tuple.h>

#include <atomic>
#include <cassert>
#include <memory>

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

    void push_to_local(reference value) {
        push_to_local(value_traits_.to_node_ptr(value));
    }

    void push_to_global(reference value) {
        push_to_global(value_traits_.to_node_ptr(value));
    }

    pointer pop() noexcept {
        erasure_heads();
        node_ptr result = pop_from_local();
        return value_traits_.to_value_ptr(result);
    }

    bool empty() const noexcept {
        if (local_head) {
            return true;
        } else {
            return global_head.load(std::memory_order_relaxed);
        }
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

    void erasure_heads() noexcept {
        if (!local_head) {
            local_head = global_head.exchange(nullptr, std::memory_order_acquire);
        }
    }

private:
    std::atomic<node_ptr> global_head{};
    node_ptr local_head{};
    value_traits value_traits_;
};

}// namespace detail
}// namespace lu

#endif
