#ifndef __THREAD_LOCAL_LIST_H__
#define __THREAD_LOCAL_LIST_H__

#include <atomic>
#include <cstddef>
#include <functional>
#include <utility>


namespace lu {
    template<class NodeTraits>
    struct ThreadLocalListAlgo {
        using node_traits = NodeTraits;
        using node_ptr = typename node_traits::node_ptr;
        using const_node_ptr = typename node_traits::const_node_ptr;

        static bool is_acquired(node_ptr this_node) {
            return node_traits::is_acquired(this_node);
        }

        static bool try_acquire(node_ptr this_node) {
            return node_traits::try_acquire(this_node);
        }

        static void release(node_ptr this_node) {
            node_traits::release(false);
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

        using node_traits = typename Types::node_traits;
        using node_ptr = typename Types::node_ptr;

    private:
        explicit ThreadLocalListIterator(node_ptr current_node) noexcept
            : current_node_(current_node) {}

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
            return &current_node_->value;
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

        using node_traits = typename Types::node_traits;
        using node_ptr = typename Types::node_ptr;

    private:
        explicit ThreadLocalListConstIterator(node_ptr current_node) noexcept
            : current_node_(current_node) {}

    public:
        ThreadLocalListConstIterator() noexcept = default;

        ThreadLocalListConstIterator(const NonConstIter &other) noexcept
            : current_node_(other.current_node_) {}

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
            return &current_node_->value;
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
    };

    class ThreadLocalNode {
        friend class ThreadLocalNodeTraits;

        using pointer = ThreadLocalNode *;
        using const_pointer = const ThreadLocalNode *;

        pointer next{};
        std::atomic<bool> is_active{true};
    };

    struct ThreadLocalNodeTraits {
        using node = ThreadLocalNode;
        using node_ptr = typename node::pointer;
        using const_node_ptr = typename node::const_pointer;

        static node_ptr get_next(const_node_ptr this_node) {
            return this_node->next;
        }

        static void set_next(node_ptr this_node, node_ptr next) {
            this_node->next = next;
        }

        static bool is_acquired(node_ptr this_node, std::memory_order order = std::memory_order_relaxed) {
            return this_node->is_active.load(order);
        }

        static bool try_acquire(node_ptr this_node, std::memory_order order = std::memory_order_acquire) {
            return !this_node->is_active.exchange(true, order);
        }

        static void release(node_ptr this_node, std::memory_order order = std::memory_order_release) {
            this_node->is_active.store(false, order);
        }
    };

    template<class ValueType>
    class ThreadLocalList {
    private:
        using Self = ThreadLocalList<ValueType>;
        using Algo = ThreadLocalListAlgo<ThreadLocalNodeTraits>;

    public:
        using value_type = ValueType;

        using pointer = value_type *;
        using const_pointer = const value_type *;
        using difference_type = std::ptrdiff_t;

        using reference = value_type &;
        using const_reference = const value_type &;

        using iterator = ThreadLocalListIterator<Self>;
        using const_iterator = ThreadLocalListConstIterator<Self>;

        using node_traits = ThreadLocalNodeTraits;
        using node = typename node_traits::node;
        using node_ptr = typename node_traits::node_ptr;
        using const_node_ptr = typename node_traits::const_node_ptr;

    private:
        struct ThreadLocalOwner {
            explicit ThreadLocalOwner(ThreadLocalList &list)
                : list(list) {}

            ~ThreadLocalOwner() {
                if (node) {
                    list.detach(node);
                }
            }

            ThreadLocalList &list;
            node_ptr node{};
        };

    public:
        template<class CreateFunc, class DestructFunc>
        ThreadLocalList(CreateFunc creator, DestructFunc destruct)
            : creator_(std::move(creator)),
              destructor_(std::move(destruct)) {}

        ThreadLocalList(const ThreadLocalList &) = delete;

        ThreadLocalList(ThreadLocalList &&) = delete;

        ~ThreadLocalList() {
            clear();
        }

    public:
        bool try_acquire(iterator item) {
            auto node = item.current_node_;
            return Algo::try_acquire(node);
        }

        bool is_acquired(const_iterator item) const {
            auto node = item.current_node_;
            return Algo::is_acquired(node);
        }

        void release(iterator item) {
            auto node = item.current_node_;
            Algo::release(node);
        }

        void attach() {
            get_thread_local();
        }

        void detach() {
            detach(get_thread_local().current_node_);
        }

        void clear() {
            auto head = head_.load(std::memory_order_acquire);
            while (head) {
                auto next = node_traits::get_next(head);
                detach(head);
                delete head;
                head = next;
            }
        }

        iterator get_thread_local() {
            static thread_local ThreadLocalOwner owner(*this);
            if (!owner.node) [[unlikely]] {
                owner.node = find_or_create();
            }
            return iterator(owner.node);
        }

        iterator begin() {
            auto head = head_.load(std::memory_order_acquire);
            return iterator(head);
        }

        iterator end() {
            return iterator();
        }

        const_iterator cbegin() const {
            auto head = head_.load(std::memory_order_acquire);
            return const_iterator(head);
        }

        const_iterator cend() const {
            return const_iterator();
        }

        const_iterator begin() const {
            return cbegin();
        }

        const_iterator end() const {
            return cend();
        }

    private:
        void detach(node_ptr node) {
            destructor_(static_cast<value_type *>(node));
            Algo::release(node);
        }

        node_ptr find_or_create() {
            auto found = Algo::find_free(head_);
            if (found) {
                return found;
            } else {
                auto new_node = creator_();
                Algo::push(head_, new_node);
                return new_node;
            }
        }

    private:
        std::function<value_type *()> creator_;
        std::function<void(value_type *)> destructor_;
        std::atomic<node_ptr> head_{};
    };

    template<class ValueType>
    using thread_local_list = ThreadLocalList<ValueType>;
}// namespace lu

#endif