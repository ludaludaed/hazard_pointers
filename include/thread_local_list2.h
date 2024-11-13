#ifndef __THREAD_LOCAL_LIST_H__
#define __THREAD_LOCAL_LIST_H__

#include "activae_list.h"
#include "fixed_size_function.h"
#include "intrusive/options.h"
#include "intrusive/unordered_set.h"
#include <atomic>
#include <cstdint>
#include <type_traits>
namespace lu {
    template<class Tag = DefaultHookTag>
    class ThreadLocalListHook : public lu::unordered_set_base_hook<lu::tag<Tag>>,
                                public lu::active_list_base_hook<lu::tag<Tag>> {

        template<class, class>
        friend class ThreadLocalList;

        using Self = ThreadLocalListHook<Tag>;

        using SetHook = lu::unordered_set_base_hook<lu::tag<Tag>>;
        using ListHook = lu::active_list_base_hook<lu::tag<Tag>>;

        using SetHook::as_node_ptr;
        using SetHook::is_linked;
        using SetHook::unique;
        using SetHook::unlink;

    private:
        void IncRef(std::size_t refs = 1) {
            ref_count_.fetch_add(refs, std::memory_order_relaxed);
        }

        void DecRef(std::size_t refs = 1) {
            if (ref_count_.fetch_sub(refs, std::memory_order_release) == refs) {
                std::atomic_thread_fence(std::memory_order_acquire);
                deleter_(this);
            }
        }

    private:
        std::uintptr_t key_{};
        std::atomic<std::size_t> ref_count_{};
        lu::fixed_size_function<void(void *), 64> deleter_{};
    };

    template<class ValueType, class Tag = DefaultHookTag>
    class ThreadLocalList {
        using Hook = ThreadLocalListHook<Tag>;

        struct KeyOfValue {
            using type = std::uintptr_t;

            std::uintptr_t operator()(const Hook &value) {
                return value.key_;
            }
        };

        using UnorderedSet = lu::unordered_set<ValueType,
                                               lu::base_hook<typename Hook::SetHook>,
                                               lu::key_of_value<KeyOfValue>>;

        using ActiveList = lu::active_list<ValueType, lu::base_hook<lu::active_list_base_hook<lu::tag<Tag>>>>;

        static_assert(std::is_base_of_v<ThreadLocalListHook<Tag>, ValueType>, "ValueType must be inherited from ThreadLocalListHook");

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

            ThreadLocalOwner()
                : set(BucketTraits(buckets.data(), buckets.size())) {}

            ~ThreadLocalOwner() {
                auto current = set.begin();
                while (current != set.end()) {
                    auto prev = current++;
                    set.erase(prev);
                    prev->release();
                    prev->DecrementRef();
                }
            }

            void insert(reference value) {
                value.IncRef();
                set.insert(value);
            }

            void detach(std::uintptr_t key) {
                auto found = set.find(key);
                if (found != set.end()) {
                    set.erase(found);
                    found->DectRef();
                }
            }

        private:
            UnorderedSet set;
            Buckets buckets;
        };

    private:
        ActiveList list_;
    };
}// namespace lu

#endif