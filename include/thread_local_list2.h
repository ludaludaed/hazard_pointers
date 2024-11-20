#ifndef __THREAD_LOCAL_LIST_H__
#define __THREAD_LOCAL_LIST_H__

#include "activae_list.h"
#include "fixed_size_function.h"
#include "intrusive/options.h"
#include "intrusive/unordered_set.h"
#include "thread_local_list.h"
#include "utils.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <type_traits>


namespace lu {
    namespace detail {
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

        struct KeyOfValue {
            using type = std::uintptr_t;

            template<class Hook>
            std::uintptr_t operator()(const Hook &value) const {
                return value.key;
            }
        };
    }// namespace detail

    class ThreadLocalListHook : public lu::unordered_set_base_hook<>,
                                public lu::active_list_base_hook<> {

        template<class>
        friend class ThreadLocalList;

        friend class detail::KeyOfValue;

        std::uintptr_t key{};
    };

    template<class ValueType>
    class ThreadLocalList {
        using UnorderedSet = lu::unordered_set<ValueType, lu::key_of_value<detail::KeyOfValue>>;
        using ActiveList = lu::active_list<ValueType>;

    public:
        using value_type = ValueType;

        using pointer = typename ActiveList::pointer;
        using const_pointer = typename ActiveList::const_pointer;
        using reference = typename ActiveList::reference;
        using const_reference = typename ActiveList::const_reference;

        using iterator = typename ActiveList::iterator;
        using const_iterator = typename ActiveList::const_iterator;

    private:
        class ThreadLocalOwner {
            using BucketTraits = typename UnorderedSet::bucket_traits;
            using BucketType = typename UnorderedSet::bucket_type;

            using Buckets = std::array<BucketType, 8>;

        public:
            explicit ThreadLocalOwner()
                : set_(BucketTraits(buckets_.data(), buckets_.size())) {}

            ~ThreadLocalOwner() {
                auto current = set_.begin();
                while (current != set_.end()) {
                    auto prev = current++;
                    auto list = get_list_by_value(*prev);
                    list->detach_value(*prev);
                }
            }

            pointer get_entry(std::uintptr_t key) noexcept {
                auto found = set_.find(key);
                if (found == set_.end()) {
                    return {};
                }
                return found.operator->();
            }

            bool contains(std::uintptr_t key) const noexcept {
                return set_.contains(key);
            }

            void attach(reference value) noexcept {
                set_.insert(value);
            }

            void detach(std::uintptr_t key) noexcept {
                auto found = set_.find(key);
                if (found != set_.end()) [[likely]] {
                    auto& value = *found;
                    set_.erase(found);
                    auto list = get_list_by_value(value);
                    list->detach_value(value);
                }
            }

        private:
            Buckets buckets_{};
            UnorderedSet set_;
        };

    public:
        template<class Detacher = detail::DefaultDetacher<pointer>,
                 class Creator = detail::DefaultCreator<pointer>,
                 class Deleter = detail::DefaultDeleter<pointer>>
        explicit ThreadLocalList(Detacher detacher = {},
                                 Creator creator = {},
                                 Deleter deleter = {})
            : detacher_(std::move(detacher)),
              creator_(std::move(creator)),
              deleter_(std::move(deleter)) {}

        ThreadLocalList(const ThreadLocalList &) = delete;

        ThreadLocalList(ThreadLocalList &&) = delete;

        ~ThreadLocalList() {
            auto current = list_.begin();
            while (current != list_.end()) {
                auto prev = current++;
                bool acquired = prev->is_acquired(std::memory_order_acquire);
                assert(!acquired && "Can't clear while all threads aren't detached");
                UNUSED(acquired);
                deleter_(prev.operator->());
            }
        }

    private:
        static ThreadLocalList *get_list_by_value(reference value) {
            return reinterpret_cast<ThreadLocalList *>(value.key);
        }

        std::uintptr_t get_key() {
            return reinterpret_cast<std::uintptr_t>(this);
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

        void detach_value(reference value) {
            detacher_(&value);
            value.release();
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
            auto key = get_key();
            if (!owner.contains(key)) [[likely]] {
                auto new_item = find_or_create();
                owner.attach(*new_item);
            }
        }

        void detach_thread() {
            auto &owner = get_owner();
            auto key = get_key();
            owner.detach(key);
        }

        reference get_thread_local() {
            auto &owner = get_owner();
            auto key = get_key();
            auto result = owner.get_entry(key);
            if (!result) [[unlikely]] {
                result = find_or_create();
                owner.attach(*result);
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