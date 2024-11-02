#ifndef __THREAD_LOCAL_LIST_H__
#define __THREAD_LOCAL_LIST_H__

#include <atomic>
#include <cstddef>
#include <functional>
#include <utility>


namespace lu {
    template<class ValueType>
    struct ThreadLocalNode {
        using pointer = ThreadLocalNode *;
        using const_pointer = const ThreadLocalNode *;

        template<class... Args>
        explicit ThreadLocalNode(Args&&... args)
            : value(std::forward<Args>(args)...) {}

        bool try_acquire() {
            return !is_active.exchange(true, std::memory_order_acquire);
        }

        bool is_acquired() const {
            return is_active.load(std::memory_order_relaxed);
        }

        void release() {
            return is_active.store(false, std::memory_order_release);
        }

    public:
        std::atomic<bool> is_active{true};
        pointer next{};
        ValueType value;
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
            current_node_ = current_node_->next;
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
            current_node_ = current_node_->next;
        }

    private:
        node_ptr current_node_{};
    };

    template<class ValueType>
    class ThreadLocalList {
    private:
        using Self = ThreadLocalList<ValueType>;

    public:
        using value_type = ValueType;

        using pointer = value_type *;
        using const_pointer = const value_type *;
        using difference_type = std::ptrdiff_t;

        using reference = value_type &;
        using const_reference = const value_type &;

        using iterator = ThreadLocalListIterator<Self>;
        using const_iterator = ThreadLocalListConstIterator<Self>;

        using node = ThreadLocalNode<ValueType>;
        using node_ptr = typename node::pointer;
        using const_node_ptr = typename node::const_pointer;

    private:
        struct ThreadLocalOwner {
            explicit ThreadLocalOwner(ThreadLocalList& list) 
                : list(list) {}

            ~ThreadLocalOwner() {
                if (node) {
                    list.detach(node);
                }
            }

            ThreadLocalList& list;
            node_ptr node{};
        };

    public:
        template<class ConstructFunc, class DestructFunc>
        ThreadLocalList(ConstructFunc construct, DestructFunc destruct) 
            : constructor_(std::move(construct)),
              destructor_(std::move(destruct)) {}

        ThreadLocalList(const ThreadLocalList &) = delete;

        ThreadLocalList(ThreadLocalList &&) = delete;

        ~ThreadLocalList() {
            clear();
        }

    public:
        bool try_acquire(iterator item) {
            auto node = item.current_node_;
            return node->try_acquire();
        }

        bool is_acquired(const_iterator item) const {
            auto node = item.current_node_;
            return node->is_acquired();
        }

        void release(iterator item) {
            auto node = item.current_node_;
            node->release();
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
                auto next = head->next;
                detach(head);
                delete head;
                head = next;
            }
        }

        iterator get_thread_local() {
            static thread_local ThreadLocalOwner owner(*this);
            if (!owner.node) {
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
            destructor_(&node->value);
            node->release();
        }

        node_ptr find_or_create() {
            auto found = find_free();
            if (found) {
                return found;
            } else {
                auto new_node = new node(*this);
                push_node(new_node);
                return new_node;
            }
        }

        void push_node(node_ptr node) {
            auto head = head_.load(std::memory_order_relaxed);
            do {
                node->next = head;
            } while (!head_.compare_exchange_weak(head, node, std::memory_order_release));
        }

        node_ptr find_free() {
            auto head = head_.load(std::memory_order_acquire);
            while (head) {
                if (head->try_acquire()) {
                    break;
                }
                head = head->next;
            }
            return head;
        }

    private:
        std::function<void(value_type*)> constructor_;
        std::function<void(value_type*)> destructor_;
        std::atomic<node_ptr> head_{};
    };

    template<class ValueType>
    using thread_local_list = ThreadLocalList<ValueType>;
}// namespace lu

#endif