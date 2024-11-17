#ifndef __THREAD_LOCAL_LIST_H__
#define __THREAD_LOCAL_LIST_H__

#include "activae_list.h"
#include "fixed_size_function.h"
#include "intrusive/options.h"
#include "intrusive/unordered_set.h"
#include "thread_local_list.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <type_traits>
namespace lu {
    class ThreadLocalListHook : public lu::unordered_set_base_hook<>,
                                public lu::active_list_base_hook<> {

        template<class>
        friend class ThreadLocalList;

        std::uintptr_t key{};
    };

    template<class ValueType>
    class ThreadLocalList {
        struct KeyOfValue {
            using type = std::uintptr_t;

            template<class Hook>
            std::uintptr_t operator()(const Hook &value) const {
                return value.key;
            }
        };

        using UnorderedSet = lu::unordered_set<ValueType, lu::key_of_value<KeyOfValue>>;
        using ActiveList = lu::active_list<ValueType>;

        static_assert(std::is_base_of_v<ThreadLocalListHook, ValueType>, "ValueType must be inherited from ThreadLocalListHook");

    public:
        using value_type = ValueType;

        using pointer = typename ActiveList::pointer;
        using const_pointer = typename ActiveList::const_pointer;
        using reference = typename ActiveList::reference;
        using const_reference = typename ActiveList::const_reference;

        using iterator = typename ActiveList::iterator;
        using const_iterator = typename ActiveList::const_iterator;

    private:
        struct ThreadLocalOwner {
            using BucketTraits = typename UnorderedSet::bucket_traits;
            using BucketType = typename UnorderedSet::bucket_type;

            using Buckets = std::array<BucketType, 8>;

            explicit ThreadLocalOwner()
                : set_(BucketTraits(buckets_.data(), buckets_.size())) {}

            ~ThreadLocalOwner() {
                auto cur = set_.begin();
                while (cur != set_.end()) {
                    auto prev = cur++;
                    detach_value(*prev);
                }
            }

            void insert(reference value) noexcept {
                set_.insert(value);
            }

            pointer get(std::uintptr_t key) noexcept {
                auto found = set_.find(key);
                if (found == set_.end()) {
                    return {};
                }
                return found.operator->();
            }

            bool contains(std::uintptr_t key) const noexcept {
                return set_.contains(key);
            }

            void erase(std::uintptr_t key) noexcept {
                auto found = set_.find(key);
                if (found != set_.end()) {
                    detach_value(*found);
                    set_.erase(found);
                }
            }

        private:
            Buckets buckets_{};
            UnorderedSet set_;
        };

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
        template<class Detacher = DefaultDetacher<pointer>,
                 class Creator = DefaultCreator<pointer>,
                 class Deleter = DefaultDeleter<pointer>>
        explicit ThreadLocalList(Detacher detacher = {},
                                 Creator creator = {},
                                 Deleter deleter = {})
            : detacher_(std::move(detacher)),
              creator_(std::move(creator)),
              deleter_(std::move(deleter)) {}

        ThreadLocalList(const ThreadLocalList &) = delete;

        ThreadLocalList(ThreadLocalList &&) = delete;

        ~ThreadLocalList() {
            auto cur = list_.begin();
            while (cur != list_.end()) {
                auto prev = ++cur;
                bool acquired = prev->is_acquired(std::memory_order_acquire);
                assert(!acquired && "Can't clear while all threads aren't detached");
                deleter_(prev.operator->());
            }
        }

    private:
        static ThreadLocalList *value_to_list(reference value) {
            return reinterpret_cast<ThreadLocalList *>(value.key);
        }

        static void detach_value(reference value) {
            auto list = value_to_list(value);
            list->detacher_(&value);
            value.release();
        }

        ThreadLocalOwner &get_owner() {
            static thread_local ThreadLocalOwner owner;
            return owner;
        }

        pointer find_or_create() {
            auto found = list_.find_free();
            if (found != list_.end()) {
                return found.operator->();
            } else {
                auto new_item = creator_();
                new_item->key = reinterpret_cast<std::uintptr_t>(this);
                list_.push(*new_item);
                return new_item;
            }
        }

    public:
        bool try_acquire(reference item) {
            return list_.try_acquire(item);
        }

        bool is_acquired(reference item, std::memory_order order = std::memory_order_relaxed) const {
            return list_.is_acquired(item, order);
        }

        void release(reference item) {
            list_.release(item);
        }

        void attach_thread() {
            auto &owner = get_owner();
            auto key = reinterpret_cast<std::uintptr_t>(this);
            if (!owner.contains(key)) [[likely]] {
                owner.insert(*find_or_create());
            }
        }

        void detach_thread() {
            auto &owner = get_owner();
            auto key = reinterpret_cast<std::uintptr_t>(this);
            owner.erase(key);
        }

        reference get_thread_local() {
            auto &owner = get_owner();
            auto key = reinterpret_cast<std::uintptr_t>(this);
            auto result = owner.get(key);
            if (!result) [[unlikely]] {
                result = find_or_create();
                owner.insert(*result);
            }
            return *result;
        }

        iterator begin() noexcept {
            return list_.begin();
        }

        iterator end() noexcept {
            return list_.end();
        }

        const_iterator cbegin() const noexcept {
            return list_.cbegin();
        }

        const_iterator cend() const noexcept {
            return list_.end();
        }

        const_iterator begin() const noexcept {
            return list_.begin();
        }

        const_iterator end() const noexcept {
            return list_.end();
        }

    private:
        ActiveList list_{};
        lu::fixed_size_function<pointer(), 64> creator_;
        lu::fixed_size_function<void(pointer), 64> deleter_;
        lu::fixed_size_function<void(pointer), 64> detacher_;
    };

    template<class ValueType>
    using thread_local_list = ThreadLocalList<ValueType>;

    using thread_local_list_base_hook = ThreadLocalListHook;
}// namespace lu

#endif