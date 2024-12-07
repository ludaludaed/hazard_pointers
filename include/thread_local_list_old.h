#ifndef __THREAD_LOCAL_LIST_H__
#define __THREAD_LOCAL_LIST_H__

#include "fixed_size_function.h"
#include "intrusive/base_value_traits.h"
#include "intrusive/empty_base_holder.h"
#include "intrusive/generic_hook.h"
#include "intrusive/get_traits.h"
#include "intrusive/node_holder.h"
#include "intrusive/pack_options.h"
#include "intrusive/utils.h"
#include "utils.h"

#include <atomic>
#include <cassert>
#include <memory>
#include <type_traits>
#include <utility>


namespace lu {
    template<class NodeTraits>
    struct ThreadLocalListAlgo {
        using node_traits = NodeTraits;
        using node_ptr = typename node_traits::node_ptr;
        using const_node_ptr = typename node_traits::const_node_ptr;

        static bool is_acquired(node_ptr this_node, std::memory_order order = std::memory_order_relaxed) {
            return node_traits::load_active(this_node, order);
        }

        static bool try_acquire(node_ptr this_node) {
            return !node_traits::exchange_active(this_node, true, std::memory_order_acquire);
        }

        static void release(node_ptr this_node) {
            node_traits::store_active(this_node, false, std::memory_order_release);
        }

        static node_ptr find_free(std::atomic<node_ptr> &head) {
            node_ptr current = head.load(std::memory_order_acquire);
            while (current) {
                if (try_acquire(current)) {
                    break;
                }
                current = node_traits::get_next(current);
            }
            return current;
        }

        static void push_front(std::atomic<node_ptr> &head, node_ptr new_node) {
            node_traits::exchange_active(new_node, true, std::memory_order_acquire);
            node_ptr current = head.load(std::memory_order_relaxed);
            do {
                node_traits::set_next(new_node, current);
            } while (!head.compare_exchange_weak(current, new_node, std::memory_order_release, std::memory_order_relaxed));
        }
    };

    template<class VoidPointer>
    class ThreadLocalListNode {
        template<class>
        friend class ThreadLocalListNodeTraits;

        using pointer = typename std::pointer_traits<VoidPointer>::template rebind<ThreadLocalListNode>;
        using const_pointer = typename std::pointer_traits<pointer>::template rebind<const ThreadLocalListNode>;

        pointer next{};
        std::atomic<bool> is_active{};
    };

    template<class VoidPointer>
    struct ThreadLocalListNodeTraits {
        using node = ThreadLocalListNode<VoidPointer>;
        using node_ptr = typename node::pointer;
        using const_node_ptr = typename node::const_pointer;

        static node_ptr get_next(const_node_ptr this_node) {
            return this_node->next;
        }

        static void set_next(node_ptr this_node, node_ptr next) {
            this_node->next = next;
        }

        static bool exchange_active(node_ptr this_node, bool value, std::memory_order order) {
            return this_node->is_active.exchange(value, order);
        }

        static bool load_active(node_ptr this_node, std::memory_order order) {
            return this_node->is_active.load(order);
        }

        static void store_active(node_ptr this_node, bool value, std::memory_order order) {
            this_node->is_active.store(value, order);
        }
    };

    template<class VoidPointer, class Tag>
    class ThreadLocalListHook : public NodeHolder<ThreadLocalListNode<VoidPointer>, Tag> {
        using NodeTraits = ThreadLocalListNodeTraits<VoidPointer>;
        using Algo = ThreadLocalListAlgo<NodeTraits>;

    public:
        using node_traits = NodeTraits;

        using node = typename node_traits::node;
        using node_ptr = typename node_traits::node_ptr;
        using const_node_ptr = typename node_traits::const_node_ptr;

        using hook_tags = detail::HookTags<NodeTraits, Tag, false>;

    public:
        bool try_acquire() {
            return Algo::try_acquire(as_node_ptr());
        }

        bool is_acquired(std::memory_order order = std::memory_order_relaxed) {
            return Algo::is_acquired(as_node_ptr(), order);
        }

        void release() {
            Algo::release(as_node_ptr());
        }

        node_ptr as_node_ptr() noexcept {
            return std::pointer_traits<node_ptr>::pointer_to(static_cast<node &>(*this));
        }

        const_node_ptr as_node_ptr() const noexcept {
            return std::pointer_traits<const_node_ptr>::pointer_to(static_cast<const node &>(*this));
        }
    };

    template<class Types>
    class ThreadLocalListIterator {
        template<class>
        friend class ThreadLocalList;

        template<class>
        friend class ThreadLocalListConstIterator;

    public:
        using value_type = typename Types::value_type;
        using pointer = typename Types::pointer;
        using reference = typename Types::reference;
        using difference_type = typename Types::difference_type;
        using iterator_category = std::forward_iterator_tag;

        using value_traits = typename Types::value_traits;
        using value_traits_ptr = typename Types::value_traits_ptr;

        using node_traits = typename Types::node_traits;
        using node_ptr = typename Types::node_ptr;

    private:
        explicit ThreadLocalListIterator(node_ptr current_node, value_traits_ptr value_traits) noexcept
            : current_node_(current_node),
              value_traits_(value_traits) {}

    public:
        ThreadLocalListIterator() noexcept = default;

        ThreadLocalListIterator &operator++() noexcept {
            Increment();
            return *this;
        }

        ThreadLocalListIterator operator++(int) noexcept {
            ThreadLocalListIterator result = *this;
            Increment();
            return result;
        }

        inline reference operator*() const noexcept {
            return *operator->();
        }

        inline pointer operator->() const noexcept {
            return value_traits_->to_value_ptr(current_node_);
        }

        friend bool operator==(const ThreadLocalListIterator &left, const ThreadLocalListIterator &right) {
            return left.current_node_ == right.current_node_;
        }

        friend bool operator!=(const ThreadLocalListIterator &left, const ThreadLocalListIterator &right) {
            return !(left == right);
        }

    private:
        void Increment() {
            current_node_ = node_traits::get_next(current_node_);
        }

    private:
        node_ptr current_node_{};
        value_traits_ptr value_traits_{};
    };

    template<class Types>
    class ThreadLocalListConstIterator {
        template<class>
        friend class ThreadLocalList;

    private:
        using NonConstIter = ThreadLocalListIterator<Types>;

    public:
        using value_type = typename Types::value_type;
        using pointer = typename Types::const_pointer;
        using reference = typename Types::const_reference;
        using difference_type = typename Types::difference_type;
        using iterator_category = std::forward_iterator_tag;

        using value_traits = typename Types::value_traits;
        using value_traits_ptr = typename Types::value_traits_ptr;

        using node_traits = typename Types::node_traits;
        using node_ptr = typename Types::node_ptr;

    private:
        explicit ThreadLocalListConstIterator(node_ptr current_node, value_traits_ptr value_traits) noexcept
            : current_node_(current_node),
              value_traits_(value_traits) {}

    public:
        ThreadLocalListConstIterator() noexcept = default;

        ThreadLocalListConstIterator(const NonConstIter &other) noexcept
            : current_node_(other.current_node_),
              value_traits_(other.value_traits_) {}

        ThreadLocalListConstIterator &operator++() noexcept {
            Increment();
            return *this;
        }

        ThreadLocalListConstIterator operator++(int) noexcept {
            ThreadLocalListConstIterator result = *this;
            Increment();
            return result;
        }

        inline reference operator*() const noexcept {
            return *operator->();
        }

        inline pointer operator->() const noexcept {
            return value_traits_->to_value_ptr(current_node_);
        }

        friend bool operator==(const ThreadLocalListConstIterator &left, const ThreadLocalListConstIterator &right) {
            return left.current_node_ == right.current_node_;
        }

        friend bool operator!=(const ThreadLocalListConstIterator &left, const ThreadLocalListConstIterator &right) {
            return !(left == right);
        }

    private:
        void Increment() {
            current_node_ = node_traits::get_next(current_node_);
        }

    private:
        node_ptr current_node_{};
        value_traits_ptr value_traits_{};
    };

    template<class ValueTraits>
    class ThreadLocalList : private detail::EmptyBaseHolder<ValueTraits> {
        using ValueTraitsHolder = detail::EmptyBaseHolder<ValueTraits>;

        using Self = ThreadLocalList<ValueTraits>;
        using Algo = ThreadLocalListAlgo<typename ValueTraits::node_traits>;

        template<class Pointer>
        struct DefaultDetacher {
            void operator()(Pointer value) const {}
        };

        template<class Pointer>
        struct DefaultCreator {
            static_assert(std::is_same_v<get_void_ptr_t<Pointer>, void *>, "The default creator can only work with void*");

            using value_type = typename std::pointer_traits<Pointer>::element_type;

            Pointer operator()() const {
                return new value_type();
            }
        };

        template<class Pointer>
        struct DefaultDeleter {
            static_assert(std::is_same_v<get_void_ptr_t<Pointer>, void *>, "The default deleter can only work with void*");

            using value_type = typename std::pointer_traits<Pointer>::element_type;

            void operator()(Pointer value) const {
                delete value;
            }
        };

    public:
        using value_type = typename ValueTraits::value_type;

        using pointer = typename ValueTraits::pointer;
        using const_pointer = typename ValueTraits::const_pointer;
        using difference_type = typename std::pointer_traits<pointer>::difference_type;

        using reference = value_type &;
        using const_reference = const value_type &;

        using iterator = ThreadLocalListIterator<Self>;
        using const_iterator = ThreadLocalListConstIterator<Self>;

        using node_traits = typename ValueTraits::node_traits;
        using node = typename node_traits::node;
        using node_ptr = typename node_traits::node_ptr;
        using const_node_ptr = typename node_traits::const_node_ptr;

        using value_traits = ValueTraits;
        using value_traits_ptr = const value_traits *;

    private:
        struct ThreadLocalOwner {
            ThreadLocalOwner(Self &list)
                : list(list) {}

            ~ThreadLocalOwner() {
                if (node) {
                    list.detach_thread();
                }
            }

            Self &list;
            node_ptr node{};
        };

    public:
        template<class Detacher = DefaultDetacher<pointer>,
                 class Creator = DefaultCreator<pointer>,
                 class Deleter = DefaultDeleter<pointer>>
        explicit ThreadLocalList(Detacher detacher = {},
                                 Creator creator = {},
                                 Deleter deleter = {},
                                 value_traits value_traits = {})
            : detacher_(std::move(detacher)),
              creator_(std::move(creator)),
              deleter_(std::move(deleter)),
              ValueTraitsHolder(std::move(value_traits)) {}

        ThreadLocalList(const ThreadLocalList &) = delete;

        ThreadLocalList(ThreadLocalList &&) = delete;

        ~ThreadLocalList() {
            const value_traits &_value_traits = GetValueTraits();
            auto current = head_.exchange({}, std::memory_order_acquire);
            while (current) {
                auto next = node_traits::get_next(current);
                bool acquired = Algo::is_acquired(current, std::memory_order_acquire);
                assert(!acquired && "Can't clear while all threads aren't detached");
                UNUSED(acquired);
                deleter_(_value_traits.to_value_ptr(current));
                current = next;
            }
        }

    private:
        inline value_traits_ptr GetValueTraitsPtr() const noexcept {
            return std::pointer_traits<value_traits_ptr>::pointer_to(ValueTraitsHolder::get());
        }

        inline const value_traits &GetValueTraits() const noexcept {
            return ValueTraitsHolder::get();
        }

        inline ThreadLocalOwner &GetOwner() {
            static thread_local ThreadLocalOwner owner(*this);
            return owner;
        }

        node_ptr FindOrCreate() {
            auto found = Algo::find_free(head_);
            if (found) {
                return found;
            } else {
                auto new_node = creator_();
                Algo::push_front(head_, new_node);
                return new_node;
            }
        }

    public:
        bool try_acquire(reference item) {
            const value_traits &_value_traits = GetValueTraits();
            return Algo::try_acquire(_value_traits.to_node_ptr(item));
        }

        bool is_acquired(reference item, std::memory_order order = std::memory_order_relaxed) const {
            const value_traits &_value_traits = GetValueTraits();
            return Algo::is_acquired(_value_traits.to_node_ptr(item), order);
        }

        void release(reference item) {
            const value_traits &_value_traits = GetValueTraits();
            Algo::release(_value_traits.to_node_ptr(item));
        }

        void attach_thread() {
            auto &owner = GetOwner();
            if (!owner.node) [[likely]] {
                owner.node = FindOrCreate();
            }
        }

        void detach_thread() {
            const value_traits &_value_traits = GetValueTraits();
            auto &owner = GetOwner();
            detacher_(_value_traits.to_value_ptr(owner.node));
            Algo::release(owner.node);
            owner.node = {};
        }

        reference get_thread_local() {
            const value_traits &_value_traits = GetValueTraits();
            auto &owner = GetOwner();
            if (!owner.node) [[unlikely]] {
                owner.node = FindOrCreate();
            }
            return *_value_traits.to_value_ptr(owner.node);
        }

        iterator begin() noexcept {
            auto head = head_.load(std::memory_order_acquire);
            return iterator(head, GetValueTraitsPtr());
        }

        iterator end() noexcept {
            return iterator({}, GetValueTraitsPtr());
        }

        const_iterator cbegin() const noexcept {
            auto head = head_.load(std::memory_order_acquire);
            return const_iterator(head, GetValueTraitsPtr());
        }

        const_iterator cend() const noexcept {
            return const_iterator({}, GetValueTraitsPtr());
        }

        const_iterator begin() const noexcept {
            return cbegin();
        }

        const_iterator end() const noexcept {
            return cend();
        }

    private:
        std::atomic<node_ptr> head_{};
        lu::fixed_size_function<pointer(), 64> creator_;
        lu::fixed_size_function<void(pointer), 64> detacher_;
        lu::fixed_size_function<void(pointer), 64> deleter_;
    };

    struct DefaultThreadLocalListHookApplier {
        template<class ValueType>
        struct Apply {
            using type = typename HookToValueTraits<ValueType, typename ValueType::thread_local_list_default_hook>::type;
        };
    };

    template<class HookType>
    struct ThreadLocalListDefaultHook {
        using thread_local_list_default_hook = HookType;
    };

    template<class VoidPointer, class Tag>
    struct ThreadLocalListBaseHook : public ThreadLocalListHook<VoidPointer, Tag>,
                                     public std::conditional_t<std::is_same_v<Tag, DefaultHookTag>,
                                                               ThreadLocalListDefaultHook<ThreadLocalListHook<VoidPointer, Tag>>,
                                                               detail::NotDefaultHook> {};

    struct ThreadLocalListDefaults {
        using proto_value_traits = DefaultThreadLocalListHookApplier;
    };

    struct ThreadLocalListHookDefaults {
        using void_pointer = void *;
        using tag = DefaultHookTag;
    };

    template<class ValueType, class... Options>
    struct make_thread_local_list {
        using pack_options = typename GetPackOptions<ThreadLocalListDefaults, Options...>::type;

        using value_traits = typename detail::GetValueTraits<ValueType, typename pack_options::proto_value_traits>::type;

        using type = ThreadLocalList<value_traits>;
    };

    template<class... Options>
    struct make_thread_local_list_base_hook {
        using pack_options = typename GetPackOptions<ThreadLocalListHookDefaults, Options...>::type;

        using void_pointer = typename pack_options::void_pointer;
        using tag = typename pack_options::tag;

        using type = ThreadLocalListBaseHook<void_pointer, tag>;
    };

    template<class ValueType, class... Options>
    using thread_local_list = typename make_thread_local_list<ValueType, Options...>::type;

    template<class... Options>
    using thread_local_list_base_hook = typename make_thread_local_list_base_hook<Options...>::type;
}// namespace lu

#endif