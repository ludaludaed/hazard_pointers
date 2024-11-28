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
    }// namespace detail

    class ThreadLocalListHook
        : public lu::unordered_set_base_hook<lu::is_auto_unlink<false>>,
          public lu::active_list_base_hook<> {

        template<class>
        friend class ThreadLocalList;

        void *key{};
    };

    template<class ValueType>
    class ThreadLocalList {
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
        using list_pointer = const ThreadLocalList *;

        struct KeyOfValue {
            using type = list_pointer;

            type operator()(const ThreadLocalListHook &value) const {
                return reinterpret_cast<type>(value.key);
            }
        };

        class ThreadLocalOwner {
            using UnorderedSet = lu::unordered_set<
                    value_type,
                    lu::key_of_value<KeyOfValue>,
                    lu::is_power_2_buckets<true>,
                    lu::hash<detail::PointerHash>>;

            using BucketTraits = typename UnorderedSet::bucket_traits;
            using BucketType = typename UnorderedSet::bucket_type;
            using Buckets = std::array<BucketType, 8>;

        public:
            ThreadLocalOwner()
                : set_(BucketTraits(buckets_.data(), buckets_.size())) {}

            ~ThreadLocalOwner() {
                auto current = set_.begin();
                while (current != set_.end()) {
                    auto prev = current++;
                    detach(*prev);
                }
            }

            pointer get_entry(list_pointer key) noexcept {
                auto found = set_.find(key);
                if (found == set_.end()) {
                    return {};
                }
                return found.operator->();
            }

            bool contains(list_pointer key) const noexcept {
                return set_.contains(key);
            }

            void attach(reference value) noexcept {
                set_.insert(value);
            }

            void detach(list_pointer key) noexcept {
                auto found = set_.find(key);
                if (found != set_.end()) [[likely]] {
                    detach(*found);
                }
            }

            void detach(reference value) {
                auto list = reinterpret_cast<list_pointer>(value.key);
                list->detacher_(&value);
                set_.erase(set_.iterator_to(value));
                value.release();
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
                UNUSED(acquired);
                assert(!acquired && "Can't clear while all threads aren't detached");
                deleter_(prev.operator->());
            }
        }

    private:
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
                new_item->key = this;
                list_.push(*new_item);
                return new_item;
            }
        }

    public:
        void attach_thread() {
            auto &owner = get_owner();
            if (!owner.contains(this)) [[likely]] {
                auto new_item = find_or_create();
                owner.attach(*new_item);
            }
        }

        void detach_thread() {
            auto &owner = get_owner();
            owner.detach(this);
        }

        reference get_thread_local() {
            auto &owner = get_owner();
            auto result = owner.get_entry(this);
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
            return list_.cend();
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