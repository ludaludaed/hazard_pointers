#ifndef __THREAD_LOCAL_LIST_H__
#define __THREAD_LOCAL_LIST_H__

#include <atomic>
#include <functional>


namespace lu {
    struct LazyListNode {
        using pointer = LazyListNode *;
        using const_pointer = const LazyListNode *;

        std::atomic<bool> is_active{};
        pointer next{};
    };

    template<class ValueType>
    class LazyList {
    public:
        using value_type = ValueType;

        using pointer = value_type *;
        using const_pointer = const value_type *;

        using reference = value_type &;
        using const_reference = const value_type &;

        using iterator = void;
        using const_iterator = void;

        using node = LazyListNode;
        using node_ptr = typename node::pointer;
        using const_node_ptr = typename node::const_pointer;

    public:
        template<class CtorFunc>
        explicit LazyList(CtorFunc &&constructor)
            : constructor_(std::forward<CtorFunc>(constructor)) {}

        LazyList(const LazyList &) = delete;

        LazyList(LazyList &&) = delete;

        ~LazyList() {
            clear();
        }

    public:
        void put(reference value) {
            release(&value);
        }

        reference take() {
            auto found = find_free();
            if (!found) {
                found = allocate_node();
                push_node(found);
            }
            return static_cast<ValueType *>(found);
        }

        void clear() {
            auto head = head_.load(std::memory_order_acquire);
            while (head) {
                auto next = head->next;
                deallocate_node(head);
                head = next;
            }
        }

        iterator begin();

        iterator end();

        const_iterator begin() const;

        const_iterator end() const;

    private:
        static bool try_acquire(node_ptr node) {
            return node->is_active.exchange(true, std::memory_order_acquire);
        }

        static void release(node_ptr node) {
            node->is_active.store(false, std::memory_order_release);
        }

        node_ptr allocate_node() const {
            return new value_type();
        }

        void deallocate_node(node_ptr node) const {
            delete node;
        }

        void push_node(node_ptr node) {
            auto head = head_.load(std::memory_order_relaxed);
            do {
                node->next = head;
            } while (head_.compare_exchange_weak(head, node, std::memory_order_release));
        }

        node_ptr find_free() {
            auto head = head_.load(std::memory_order_acquire);
            while (head) {
                if (try_acquire(head)) {
                    break;
                }
                head = head->next;
            }
            return head;
        }

    private:
        std::atomic<node_ptr> head_;
        std::function<ValueType()> constructor_;
    };
}// namespace lu

#endif