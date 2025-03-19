#ifndef __MPSC_FREE_LIST__
#define __MPSC_FREE_LIST__

#include <lu/intrusive/detail/compressed_tuple.h>

#include <atomic>
#include <cassert>
#include <memory>
#include <thread>


namespace lu {
namespace detail {

template<class NodeTraits>
struct MPSCFreeListAlgo {
    using node_traits = NodeTraits;

    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using const_node_ptr = typename node_traits::const_node_ptr;

    static void push_to_global(std::atomic<node_ptr> &head, node_ptr new_node) noexcept {
        node_ptr current = head.load(std::memory_order_relaxed);
        do {
            node_traits::set_next(new_node, current);
        } while (!head.compare_exchange_weak(current, new_node, std::memory_order_release, std::memory_order_relaxed));
    }

    static void push_to_local(node_ptr &head, node_ptr new_node) noexcept {
        node_traits::set_next(new_node, head);
        head = new_node;
    }

    static node_ptr pop_from_local(node_ptr &head) noexcept {
        node_ptr result = head;
        if (result) {
            head = node_traits::get_next(result);
        }
        return result;
    }

    static void erasure_heads(node_ptr &local_head, std::atomic<node_ptr> &global_head) noexcept {
        if (!local_head) {
            local_head = global_head.exchange(nullptr, std::memory_order_acquire);
        }
    }
};

template<class VoidPointer>
class MPSCFreeListNode {
    template<class>
    friend struct MPSCFreeListNodeTraits;

    using pointer = std::pointer_traits<VoidPointer>::template rebind<MPSCFreeListNode>;
    using const_pointer = std::pointer_traits<VoidPointer>::template rebind<const MPSCFreeListNode>;

    pointer next;
};

template<class VoidPointer>
struct MPSCFreeListNodeTraits {
    using node = MPSCFreeListNode<VoidPointer>;
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
class MPSCFreeList {
    using Algo = MPSCFreeListAlgo<typename ValueTraits::node_traits>;

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

private:
    struct Data {
        std::atomic<node_ptr> global_head{};
        node_ptr local_head{};
    };

public:
    explicit MPSCFreeList(const value_traits &value_traits = {}) noexcept
        : data_(Data(), std::this_thread::get_id(), value_traits) {}

    MPSCFreeList(const MPSCFreeList &) = delete;

    MPSCFreeList(MPSCFreeList &&) = delete;

    void push(reference value) noexcept {
        const value_traits &_value_traits = lu::get<ValueTraits>(data_);

        auto owner_thread_id = lu::get<std::thread::id>(data_);
        auto current_thread_id = std::this_thread::get_id();

        if (current_thread_id != owner_thread_id) {
            Algo::push_to_global(GetGlobalHead(), _value_traits.to_node_ptr(value));
        } else {
            Algo::push_to_local(GetLocalHead(), _value_traits.to_node_ptr(value));
        }
    }

    pointer pop() noexcept {
        assert(lu::get<std::thread::id>(data_) == std::this_thread::get_id());
        const value_traits &_value_traits = lu::get<ValueTraits>(data_);

        auto &local_head = GetLocalHead();
        auto &global_head = GetGlobalHead();

        Algo::erasure_heads(local_head, global_head);
        node_ptr result = Algo::pop_from_local(local_head);
        return _value_traits.to_value_ptr(result);
    }

    bool empty() const noexcept {
        auto &local_head = GetLocalHead();
        if (local_head) {
            return local_head;
        }
        auto &global_head = GetGlobalHead();
        return global_head.load(std::memory_order_relaxed);
    }

private:
    node_ptr &GetLocalHead() {
        return lu::get<Data>(data_).local_head;
    }

    std::atomic<node_ptr> &GetGlobalHead() {
        return lu::get<Data>(data_).global_head;
    }

    const node_ptr &GetLocalHead() const {
        return lu::get<Data>(data_).local_head;
    }

    const std::atomic<node_ptr> &GetGlobalHead() const {
        return lu::get<Data>(data_).global_head;
    }

private:
    lu::compressed_tuple<Data, std::thread::id, value_traits> data_;
};

}// namespace detail
}// namespace lu

#endif
