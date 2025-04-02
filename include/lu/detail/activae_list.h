#ifndef __ACTIVE_LIST_H__
#define __ACTIVE_LIST_H__

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

template <class NodeTraits>
struct ActiveListAlgo {
    using node_traits = NodeTraits;
    using node_ptr = typename node_traits::node_ptr;
    using const_node_ptr = typename node_traits::const_node_ptr;

    static void init(node_ptr this_node) noexcept {
        node_traits::set_next(this_node, node_ptr{});
        node_traits::store_active(this_node, false, std::memory_order_relaxed);
    }

    static bool is_acquired(node_ptr this_node,
                            std::memory_order order = std::memory_order_relaxed) noexcept {
        return node_traits::load_active(this_node, order);
    }

    static bool try_acquire(node_ptr this_node) noexcept {
        if (node_traits::load_active(this_node, std::memory_order_relaxed)) {
            return false;
        }
        return !node_traits::exchange_active(this_node, true, std::memory_order_acquire);
    }

    static void release(node_ptr this_node) noexcept {
        node_traits::store_active(this_node, false, std::memory_order_release);
    }

    static node_ptr try_acquire_free(std::atomic<node_ptr> &head) noexcept {
        node_ptr current = head.load(std::memory_order_acquire);
        while (current) {
            if (try_acquire(current)) {
                break;
            }
            current = node_traits::get_next(current);
        }
        return current;
    }

    static void push_front(std::atomic<node_ptr> &head, node_ptr new_node) noexcept {
        node_traits::store_active(new_node, true, std::memory_order_relaxed);
        node_ptr current = head.load(std::memory_order_relaxed);
        do {
            node_traits::set_next(new_node, current);
        } while (!head.compare_exchange_weak(current, new_node, std::memory_order_release));
    }
};

template <class VoidPointer>
class ActiveListNode {
    template <class>
    friend class ActiveListNodeTraits;

    using pointer = typename std::pointer_traits<VoidPointer>::template rebind<ActiveListNode>;
    using const_pointer = typename std::pointer_traits<pointer>::template rebind<const ActiveListNode>;

    pointer next{};
    std::atomic<bool> is_active{};
};

template <class VoidPointer>
struct ActiveListNodeTraits {
    using node = ActiveListNode<VoidPointer>;
    using node_ptr = typename node::pointer;
    using const_node_ptr = typename node::const_pointer;

    static node_ptr get_next(const_node_ptr this_node) noexcept { return this_node->next; }

    static void set_next(node_ptr this_node, node_ptr next) noexcept { this_node->next = next; }

    static bool exchange_active(node_ptr this_node, bool value, std::memory_order order) noexcept {
        return this_node->is_active.exchange(value, order);
    }

    static bool load_active(node_ptr this_node, std::memory_order order) noexcept {
        return this_node->is_active.load(order);
    }

    static void store_active(node_ptr this_node, bool value, std::memory_order order) noexcept {
        this_node->is_active.store(value, order);
    }
};

template <class VoidPointer, class Tag>
class ActiveListHook : public NodeHolder<ActiveListNode<VoidPointer>, Tag> {
    using NodeTraits = ActiveListNodeTraits<VoidPointer>;
    using NodeAlgo = ActiveListAlgo<NodeTraits>;

public:
    using node_traits = NodeTraits;

    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using const_node_ptr = typename node_traits::const_node_ptr;

    using hook_tags = HookTags<NodeTraits, Tag, false>;

public:
    ActiveListHook() noexcept { NodeAlgo::init(as_node_ptr()); }

    ActiveListHook(const ActiveListHook &) noexcept { NodeAlgo::init(as_node_ptr()); }

    ActiveListHook &operator=(const ActiveListHook &) noexcept { return *this; }

    bool try_acquire() noexcept { return NodeAlgo::try_acquire(as_node_ptr()); }

    bool is_acquired(std::memory_order order = std::memory_order_relaxed) noexcept {
        return NodeAlgo::is_acquired(as_node_ptr(), order);
    }

    void release() noexcept { NodeAlgo::release(as_node_ptr()); }

    node_ptr as_node_ptr() noexcept {
        return std::pointer_traits<node_ptr>::pointer_to(static_cast<node &>(*this));
    }

    const_node_ptr as_node_ptr() const noexcept {
        return std::pointer_traits<const_node_ptr>::pointer_to(static_cast<const node &>(*this));
    }
};

template <class Types, bool IsConst>
class ActiveListIterator {
    template <class>
    friend class ActiveList;
    friend class ActiveListIterator<Types, true>;

    class DummyNonConstIter;
    using NonConstIter =
            typename std::conditional_t<IsConst, ActiveListIterator<Types, false>, DummyNonConstIter>;

    using value_traits = typename Types::value_traits;
    using value_traits_ptr = typename Types::value_traits_ptr;

    using node_traits = typename value_traits::node_traits;
    using node_ptr = typename node_traits::node_ptr;

public:
    using value_type = typename Types::value_type;
    using pointer = std::conditional_t<IsConst, typename Types::const_pointer, typename Types::pointer>;
    using reference = std::conditional_t<IsConst, typename Types::const_reference, typename Types::reference>;
    using difference_type = typename Types::difference_type;
    using iterator_category = std::forward_iterator_tag;

private:
    ActiveListIterator(node_ptr current_node, value_traits_ptr value_traits) noexcept
        : current_node_(current_node)
        , value_traits_(value_traits) {}

public:
    ActiveListIterator() noexcept = default;

    ActiveListIterator(const NonConstIter &other)
        : current_node_(other.current_node_)
        , value_traits_(other.value_traits_) {}

    ActiveListIterator &operator=(const NonConstIter &other) noexcept {
        current_node_ = other.current_node_;
        value_traits_ = other.value_traits_;
        return *this;
    }

    ActiveListIterator &operator++() noexcept {
        Increment();
        return *this;
    }

    ActiveListIterator operator++(int) noexcept {
        ActiveListIterator result(*this);
        Increment();
        return result;
    }

    inline reference operator*() const noexcept { return *operator->(); }

    inline pointer operator->() const noexcept { return value_traits_->to_value_ptr(current_node_); }

    friend bool operator==(const ActiveListIterator &left, const ActiveListIterator &right) noexcept {
        return left.current_node_ == right.current_node_;
    }

    friend bool operator!=(const ActiveListIterator &left, const ActiveListIterator &right) noexcept {
        return !(left == right);
    }

private:
    void Increment() noexcept { current_node_ = node_traits::get_next(current_node_); }

private:
    node_ptr current_node_{};
    value_traits_ptr value_traits_{};
};

template <class ValueTraits>
class ActiveList : private ValueTraits {
    using Algo = ActiveListAlgo<typename ValueTraits::node_traits>;

public:
    using value_traits = ValueTraits;
    using node_traits = typename value_traits::node_traits;

    using value_type = typename value_traits::value_type;

    using pointer = typename value_traits::pointer;
    using const_pointer = typename value_traits::const_pointer;
    using difference_type = typename std::pointer_traits<pointer>::difference_type;

    using reference = value_type &;
    using const_reference = const value_type &;

    using iterator = ActiveListIterator<ActiveList, false>;
    using const_iterator = ActiveListIterator<ActiveList, true>;

    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using const_node_ptr = typename node_traits::const_node_ptr;

    using value_traits_ptr = const value_traits *;

public:
    explicit ActiveList(const value_traits &value_traits = {}) noexcept
        : ValueTraits(value_traits) {}

    ActiveList(const ActiveList &) = delete;

    ActiveList(ActiveList &&) = delete;

private:
    inline value_traits_ptr GetValueTraitsPtr() const noexcept {
        return std::pointer_traits<value_traits_ptr>::pointer_to(static_cast<const ValueTraits &>(*this));
    }

public:
    bool try_acquire(reference item) noexcept { return Algo::try_acquire(ValueTraits::to_node_ptr(item)); }

    bool is_acquired(reference item, std::memory_order order = std::memory_order_relaxed) const noexcept {
        return Algo::is_acquired(ValueTraits::to_node_ptr(item), order);
    }

    void release(reference item) noexcept { Algo::release(ValueTraits::to_node_ptr(item)); }

    void push(reference new_element) noexcept {
        Algo::push_front(head_, ValueTraits::to_node_ptr(new_element));
    }

    iterator try_acquire_free() noexcept {
        auto found = Algo::try_acquire_free(head_);
        return iterator(found, GetValueTraitsPtr());
    }

    iterator begin() noexcept {
        auto head = head_.load(std::memory_order_acquire);
        return iterator(head, GetValueTraitsPtr());
    }

    iterator end() noexcept { return iterator({}, GetValueTraitsPtr()); }

    const_iterator cbegin() const noexcept {
        auto head = head_.load(std::memory_order_acquire);
        return const_iterator(head, GetValueTraitsPtr());
    }

    const_iterator cend() const noexcept { return const_iterator({}, GetValueTraitsPtr()); }

    const_iterator begin() const noexcept { return cbegin(); }

    const_iterator end() const noexcept { return cend(); }

private:
    std::atomic<node_ptr> head_;
};

template <class HookType>
struct ActiveListDefaultHook {
    using active_list_default_hook = HookType;
};

template <class VoidPointer, class Tag>
struct ActiveListBaseHook
    : public ActiveListHook<VoidPointer, Tag>,
      public std::conditional_t<std::is_same_v<Tag, DefaultHookTag>,
                                ActiveListDefaultHook<ActiveListHook<VoidPointer, Tag>>, NotDefaultHook> {};

struct DefaultActiveListHook {
    template <class ValueType>
    struct GetDefaultHook {
        using type = typename ValueType::active_list_default_hook;
    };

    struct is_default_hook_tag;
};

struct ActiveListDefaults {
    using proto_value_traits = DefaultActiveListHook;
};

struct ActiveListHookDefaults {
    using void_pointer = void *;
    using tag = DefaultHookTag;
};

}// namespace detail
}// namespace lu

namespace lu {
namespace detail {

template <class... Options>
struct make_active_list_base_hook {
    using pack_options = typename GetPackOptions<ActiveListHookDefaults, Options...>::type;

    using void_pointer = typename pack_options::void_pointer;
    using tag = typename pack_options::tag;

    using type = ActiveListBaseHook<void_pointer, tag>;
};

template <class ValueType, class... Options>
struct make_active_list {
    using pack_options = typename GetPackOptions<ActiveListDefaults, Options...>::type;
    using value_traits = typename GetValueTraits<ValueType, typename pack_options::proto_value_traits>::type;

    using type = ActiveList<value_traits>;
};

}// namespace detail

template <class... Options>
using active_list_base_hook = typename detail::make_active_list_base_hook<Options...>::type;

template <class ValueType, class... Options>
using active_list = typename detail::make_active_list<ValueType, Options...>::type;

}// namespace lu

#endif
