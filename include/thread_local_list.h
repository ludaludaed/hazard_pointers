#ifndef __THREAD_LOCAL_LIST_H__
#define __THREAD_LOCAL_LIST_H__

#include "intrusive/base_value_traits.h"
#include "intrusive/empty_base_holder.h"
#include "intrusive/generic_hook.h"
#include "intrusive/node_holder.h"
#include <atomic>
#include <functional>
#include <type_traits>
#include <utility>


namespace lu {
    template<class NodeTraits>
    struct ThreadLocalListAlgo {
        using node_traits = NodeTraits;
        using node_ptr = typename node_traits::node_ptr;
        using const_node_ptr = typename node_traits::const_node_ptr;

        static bool is_acquired(node_ptr this_node) {
            return node_traits::load_active(this_node, std::memory_order_relaxed);
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
                current = node_traits::gen_next(current);
            }
            return current;
        }

        static void push(std::atomic<node_ptr> &head, node_ptr new_node) {
            node_ptr current = head.load(std::memory_order_relaxed);
            do {
                node_traits::set_next(new_node, current);
            } while (!head.compare_exchange_weak(current, new_node, std::memory_order_release, std::memory_order_relaxed));
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

    template<class VoidPointer>
    class ThreadLocalNode {
        template<class>
        friend class ThreadLocalNodeTraits;

        using pointer = typename std::pointer_traits<VoidPointer>::template rebind<ThreadLocalNode>;
        using const_pointer = typename std::pointer_traits<pointer>::template rebind<const ThreadLocalNode>;

        pointer next{};
        std::atomic<bool> is_active{true};
    };

    template<class VoidPointer>
    struct ThreadLocalNodeTraits {
        using node = ThreadLocalNode<VoidPointer>;
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

    template<class ValueTraits, class Detacher, class Creator, class Deleter>
    class ThreadLocalList : private detail::EmptyBaseHolder<ValueTraits>,
                            private detail::EmptyBaseHolder<Detacher>,
                            private detail::EmptyBaseHolder<Creator>,
                            private detail::EmptyBaseHolder<Deleter> {
    private:
        using ValueTraitsHolder = detail::EmptyBaseHolder<ValueTraits>;
        using DetacherHolder = detail::EmptyBaseHolder<Detacher>;
        using CreatorHolder = detail::EmptyBaseHolder<Creator>;
        using DeleterHolder = detail::EmptyBaseHolder<Deleter>;

        using Self = ThreadLocalList<ValueTraits, Detacher, Creator, Deleter>;
        using Algo = ThreadLocalListAlgo<typename ValueTraits::node_traits>;

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

        using detacher = Detacher;
        using creator = Creator;
        using deleter = Deleter;

    private:
        struct ThreadLocalOwner {
            explicit ThreadLocalOwner(ThreadLocalList &list)
                : list(list) {}

            ~ThreadLocalOwner() {
                if (node) {
                    list.Detach(node);
                }
            }

            ThreadLocalList &list;
            node_ptr node{};
        };

    public:
        ThreadLocalList(detacher detacher = {}, creator creator = {}, deleter deleter = {}, value_traits value_traits = {})
            : DetacherHolder(std::move(detacher)),
              CreatorHolder(std::move(creator)),
              DeleterHolder(std::move(deleter)),
              ValueTraitsHolder(std::move(value_traits)) {}

        ThreadLocalList(const ThreadLocalList &) = delete;

        ThreadLocalList(ThreadLocalList &&) = delete;

        ~ThreadLocalList() {
            clear();
        }

    private:
        inline value_traits_ptr GetValueTraitsPtr() const noexcept {
            return std::pointer_traits<value_traits_ptr>::pointer_to(ValueTraitsHolder::get());
        }

        inline const detacher &GetDetacher() const noexcept {
            return DetacherHolder::get();
        }

        inline const creator &GetCreator() const noexcept {
            return CreatorHolder::get();
        }

        inline const deleter &GetDeleter() const noexcept {
            return DeleterHolder::get();
        }

        void Detach(node_ptr node) {
            const value_traits &_value_traits = ValueTraitsHolder::get();
            GetDetacher()(_value_traits.to_value_ptr(node));
            Algo::release(node);
        }

        node_ptr FindOrCreate() {
            auto found = Algo::find_free(head_);
            if (found) {
                return found;
            } else {
                auto new_node = GetCreator()();
                Algo::push(head_, new_node);
                return new_node;
            }
        }

    public:
        bool try_acquire(iterator item) {
            return Algo::try_acquire(item.current_node_);
        }

        bool is_acquired(const_iterator item) const {
            return Algo::is_acquired(item.current_node_);
        }

        void release(iterator item) {
            Algo::release(item.current_node_);
        }

        void attach() {
            get_thread_local();
        }

        void detach() {
            Detach(get_thread_local().current_node_);
        }

        void clear() {
            const value_traits &_value_traits = ValueTraitsHolder::get();
            auto head = head_.load(std::memory_order_acquire);
            while (head) {
                auto next = node_traits::get_next(head);
                Detach(head);
                GetCreator()(_value_traits.to_value_ptr(head));
                head = next;
            }
        }

        iterator get_thread_local() {
            static thread_local ThreadLocalOwner owner(*this);
            if (!owner.node) [[unlikely]] {
                owner.node = FindOrCreate();
            }
            return iterator(owner.node, GetValueTraitsPtr());
        }

        iterator begin() {
            auto head = head_.load(std::memory_order_acquire);
            return iterator(head, GetValueTraitsPtr());
        }

        iterator end() {
            return iterator({}, GetValueTraitsPtr());
        }

        const_iterator cbegin() const {
            auto head = head_.load(std::memory_order_acquire);
            return const_iterator(head, GetValueTraitsPtr());
        }

        const_iterator cend() const {
            return const_iterator({}, GetValueTraitsPtr());
        }

        const_iterator begin() const {
            return cbegin();
        }

        const_iterator end() const {
            return cend();
        }

    private:
        std::atomic<node_ptr> head_{};
    };
    struct DefaultThreadLocalListHookApplier {
        template<class ValueType>
        struct apply {
            using type = typename hook_to_value_traits<ValueType, typename ValueType::thread_local_list_default_hook>::type;
        };
    };

    template<class HookType>
    struct ThreadLocalListDefaultHook {
        using thread_local_list_default_hook = HookType;
    };

    template<class VoidPointer, class Tag>
    class ThreadLocalListHook : public NodeHolder<ThreadLocalNode<VoidPointer>, Tag> {
        using NodeTraits = ThreadLocalNodeTraits<VoidPointer>;
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

        bool is_acquired() {
            return Algo::is_acquired(as_node_ptr());
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

    template<class VoidPointer, class Tag>
    struct ThreadLocalListBaseHook : public ThreadLocalListHook<VoidPointer, Tag>,
                                     public std::conditional_t<std::is_same_v<Tag, DefaultHookTag>, 
                                                               ThreadLocalListDefaultHook<ThreadLocalListHook<VoidPointer, Tag>>,
                                                               detail::NotDefaultHook> {};

    struct ThreadLocalListDefaults {
        using proto_value_traits = DefaultThreadLocalListHookApplier;
        using detacher = void;
        using creator = void;
        using deleter = void;
    };

    struct ThreadLocalListHookDefaults {
        using void_pointer = void *;
        using tag = DefaultHookTag;
    };
}// namespace lu

#endif