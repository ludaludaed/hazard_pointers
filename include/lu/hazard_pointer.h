#ifndef __HAZARD_POINTERS_H__
#define __HAZARD_POINTERS_H__

#include <lu/detail/shared_freelist.h>
#include <lu/detail/thread_local_list.h>
#include <lu/detail/utils.h>
#include <lu/intrusive/detail/utils.h>
#include <lu/intrusive/forward_list.h>
#include <lu/intrusive/options.h>
#include <lu/intrusive/unordered_set.h>
#include <lu/utils/marked_ptr.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>


namespace lu {

class hazard_pointer_domain;

template <class, class>
class hazard_pointer_obj_base;

}// namespace lu

namespace lu {
namespace detail {

class HazardPointerTag;

using HazardRetiresHook = lu::unordered_set_base_hook<lu::tag<HazardPointerTag>, lu::store_hash<false>,
                                                      lu::is_auto_unlink<false>>;

class HazardObject : public HazardRetiresHook {
    template <class, class>
    friend class lu::hazard_pointer_obj_base;
    friend class lu::hazard_pointer_domain;

    friend struct HazardKeyOfValue;

    using ReclaimFuncPtr = void (*)(HazardObject *value);

protected:
    HazardObject() noexcept = default;

    HazardObject(const HazardObject &other) noexcept {};

    HazardObject &operator=(const HazardObject &) { return *this; }

    ~HazardObject() { assert(!this->is_linked()); }

private:
    void reclaim() { reclaim_func_(this); }

    void set_reclaim(ReclaimFuncPtr reclaim) noexcept { reclaim_func_ = reclaim; }

    const void *get_key() const noexcept { return key_.get(); }

    void set_key(const void *key) noexcept { key_ = lu::marked_ptr<const void>(key, key_.is_marked()); }

    bool is_protected() const noexcept { return key_.is_marked(); }

    void set_protection(bool value) noexcept { key_.set_mark(value); }

private:
    ReclaimFuncPtr reclaim_func_{};
    lu::marked_ptr<const void> key_{};
};

struct HazardKeyOfValue {
    using type = const void *;

    const void *operator()(const HazardObject &value) const noexcept { return value.get_key(); }
};

struct HazardHash {
    std::size_t operator()(const void *ptr) const noexcept {
        return detail::PointerHash()(static_cast<const HazardObject *>(ptr));
    }
};

using HazardRetiresSet
        = lu::unordered_set<HazardObject, lu::base_hook<HazardRetiresHook>, lu::is_power_2_buckets<true>,
                            lu::key_of_value<HazardKeyOfValue>, lu::hash<HazardHash>>;

class HazardRetires : public HazardRetiresSet {
    using Base = HazardRetiresSet;

    using BucketType = typename Base::bucket_type;
    using BucketTraits = typename Base::bucket_traits;

public:
    using resource = std::span<BucketType>;

public:
    HazardRetires(resource buckets) noexcept
        : Base(BucketTraits(buckets.data(), buckets.size())) {
        for (std::size_t i = 0; i < buckets.size(); ++i) {
            ::new (buckets.data() + i) BucketType();
        }
    }
};

class HazardRecords;

class HazardRecord : public lu::shared_free_list_base_hook<> {
public:
    explicit HazardRecord(HazardRecords *owner) noexcept
        : owner_(owner) {}

    HazardRecord(const HazardRecord &) = delete;

    HazardRecord(HazardRecord &&) = delete;

    inline void reset(const void *ptr = {}) { protected_.store(ptr, std::memory_order_release); }

    inline const void *get() const noexcept { return protected_.load(std::memory_order_acquire); }

    inline bool empty() const noexcept { return !protected_.load(std::memory_order_acquire); }

    inline HazardRecords *get_owner() const noexcept { return owner_; }

private:
    std::atomic<const void *> protected_{};
    HazardRecords *owner_;
};

class HazardRecords : public lu::shared_free_list<HazardRecord> {
    using Base = lu::shared_free_list<HazardRecord>;

public:
    using resource = std::span<HazardRecord>;

    using value_type = HazardRecord;

    using reference = typename resource::reference;
    using const_reference = typename resource::const_reference;
    using pointer = typename resource::pointer;
    using const_pointer = typename resource::const_pointer;

    using iterator = pointer;
    using const_iterator = const_pointer;

public:
    HazardRecords(resource data) noexcept
        : data_(data) {
        for (std::size_t i = 0; i < data_.size(); ++i) {
            ::new (data_.data() + i) value_type(this);
            Base::push_to_local(data_[i]);
        }
    }

    HazardRecords(const HazardRecords &) = delete;

    HazardRecords(HazardRecords &&) = delete;

public:
    iterator begin() noexcept { return data_.data(); }

    iterator end() noexcept { return data_.data() + data_.size(); }

    const_iterator begin() const noexcept { return data_.data(); }

    const_iterator end() const noexcept { return data_.data() + data_.size(); }

private:
    resource data_;
};

}// namespace detail

class hazard_pointer_domain {
    template <class, class>
    friend class hazard_pointer_obj_base;
    friend class hazard_pointer;

    using HazardObject = detail::HazardObject;
    using HazardRecord = detail::HazardRecord;

    class HazardThreadData : public lu::thread_local_list_base_hook<HazardThreadData> {
        friend class hazard_pointer_domain;

        using HazardRecords = detail::HazardRecords;
        using HazardRetires = detail::HazardRetires;

    public:
        using records_resource = typename HazardRecords::resource;
        using retires_resource = typename HazardRetires::resource;

    public:
        HazardThreadData(hazard_pointer_domain *domain, std::size_t scan_threshold,
                         records_resource records_resource, retires_resource retires_resource) noexcept
            : domain_(domain)
            , scan_threshold_(scan_threshold)
            , records_(records_resource)
            , retires_(retires_resource) {}

        HazardThreadData(const HazardThreadData &) = delete;

        HazardThreadData(HazardThreadData &&) = delete;

        ~HazardThreadData() { clear(); }

        void clear() {
            auto current = retires_.begin();
            while (current != retires_.end()) {
                auto prev = current++;
                reclaim(*prev);
            }
        }

        void reclaim(HazardObject &retired) {
            retires_.erase(retires_.iterator_to(retired));
            retired.reclaim();
            num_of_reclaimed.fetch_add(1, std::memory_order_relaxed);
        }

        bool retire(HazardObject &retired) noexcept {
            retires_.insert(retired);
            num_of_retired.fetch_add(1, std::memory_order_relaxed);
            return retires_.size() >= scan_threshold_;
        }

        void merge(HazardThreadData &other) noexcept { retires_.merge(other.retires_); }

        HazardRecord *acquire_record() noexcept { return records_.pop(); }

        void release_record(HazardRecord *record) noexcept {
            auto owner = record->get_owner();
            if (owner == &records_) {
                owner->push_to_local(*record);
            } else {
                owner->push_to_global(*record);
            }
        }

        void on_detach() { domain_->help_scan(); }

    private:
        hazard_pointer_domain *domain_;
        std::size_t scan_threshold_;
        HazardRecords records_;
        HazardRetires retires_;

        std::atomic<std::size_t> num_of_retired;
        std::atomic<std::size_t> num_of_reclaimed;
    };

    struct Creator {
        Creator(hazard_pointer_domain *domain, std::size_t num_of_records, std::size_t num_of_retires,
                std::size_t scan_threshold)
            : domain_(domain)
            , num_of_records_(num_of_records)
            , num_of_retires_(num_of_retires)
            , scan_threshold_(scan_threshold) {}

        HazardThreadData *operator()() const {
            using records_resource = typename HazardThreadData::records_resource;
            using records_element_type = typename records_resource::element_type;

            using retires_resource = typename HazardThreadData::retires_resource;
            using retires_element_type = typename retires_resource::element_type;

            std::size_t header_size = sizeof(HazardThreadData);
            std::size_t records_resource_size = sizeof(records_element_type) * num_of_records_;
            std::size_t retires_resource_size = sizeof(retires_element_type) * num_of_retires_;

            std::size_t size = header_size + records_resource_size + retires_resource_size;

            auto blob = new std::uint8_t[size];
            auto records = reinterpret_cast<records_element_type *>(blob + header_size);
            auto retires
                    = reinterpret_cast<retires_element_type *>(blob + header_size + records_resource_size);

            records_resource _records_resource(records, num_of_records_);
            retires_resource _retires_resource(retires, num_of_retires_);

            ::new (blob) HazardThreadData(domain_, scan_threshold_, _records_resource, _retires_resource);
            auto thread_data = reinterpret_cast<HazardThreadData *>(blob);

            auto deleter = [](HazardThreadData *thread_data) noexcept {
                thread_data->~HazardThreadData();
                delete[] reinterpret_cast<std::uint8_t *>(thread_data);
            };

            thread_data->set_deleter(std::move(deleter));
            return thread_data;
        }

    private:
        hazard_pointer_domain *domain_;
        std::size_t num_of_records_;
        std::size_t num_of_retires_;
        std::size_t scan_threshold_;
    };

    static constexpr std::size_t DEFAULT_NUM_OF_RECORDS = 8;
    static constexpr std::size_t DEFAULT_NUM_OF_RETIRES = 64;
    static constexpr std::size_t DEFAULT_SCAN_THRESHOLD = 64;

public:
    hazard_pointer_domain(std::size_t num_of_records = DEFAULT_NUM_OF_RECORDS,
                          std::size_t num_of_retires = DEFAULT_NUM_OF_RETIRES,
                          std::size_t scan_threshold = DEFAULT_SCAN_THRESHOLD)
        : list_(Creator(this, num_of_records, num_of_retires, scan_threshold)) {}

    hazard_pointer_domain(const hazard_pointer_domain &) = delete;

    hazard_pointer_domain(hazard_pointer_domain &&) = delete;

    void attach_thread() { list_.attach_thread(); }

    void detach_thread() { list_.detach_thread(); }

    std::size_t num_of_retired() noexcept {
        std::size_t result{};
        for (auto it = list_.begin(); it != list_.end(); ++it) {
            result += it->num_of_retired.load(std::memory_order_relaxed);
        }
        return result;
    }

    std::size_t num_of_reclaimed() noexcept {
        std::size_t result{};
        for (auto it = list_.begin(); it != list_.end(); ++it) {
            result += it->num_of_reclaimed.load(std::memory_order_relaxed);
        }
        return result;
    }

    template <class ValueType, class Deleter = std::default_delete<ValueType>,
              class = std::enable_if_t<!std::is_base_of_v<detail::HazardObject, ValueType>>>
    void retire(ValueType *value, Deleter deleter = {}) {
        struct NonIntrusiveHazardObj : public detail::HazardObject {
            NonIntrusiveHazardObj(ValueType *value, Deleter deleter) noexcept
                : obj_(value, std::move(deleter)) {
                this->set_reclaim(reclaim_func);
                this->set_key(value);
            }

            static void reclaim_func(HazardObject *obj) { delete static_cast<NonIntrusiveHazardObj *>(obj); }

        private:
            std::unique_ptr<ValueType, Deleter> obj_;
        };

        auto retired_obj = new NonIntrusiveHazardObj(value, std::move(deleter));
        retire(retired_obj);
    }

private:
    void retire(HazardObject *retired) {
        auto &thread_data = list_.get_thread_local();
        if (thread_data.retire(*retired)) [[unlikely]] {
            scan();
        }
    }

    HazardRecord *acquire_record() {
        auto &thread_data = list_.get_thread_local();
        return thread_data.acquire_record();
    }

    void release_record(HazardRecord *record) {
        auto &thread_data = list_.get_thread_local();
        thread_data.release_record(record);
    }

    void scan() {
        auto &thread_data = list_.get_thread_local();
        auto &retires = thread_data.retires_;
        std::atomic_thread_fence(std::memory_order_seq_cst);
        for (auto current = list_.begin(); current != list_.end(); ++current) {
            auto &records = current->records_;
            for (auto record = records.begin(); record != records.end(); ++record) {
                auto found = retires.find(record->get());
                if (found != retires.end()) {
                    found->set_protection(true);
                }
            }
        }

        auto current = retires.begin();
        while (current != retires.end()) {
            auto prev = current++;
            if (prev->is_protected()) {
                prev->set_protection(false);
            } else {
                thread_data.reclaim(*prev);
            }
        }
    }

    void help_scan() {
        auto &thread_data = list_.get_thread_local();
        for (auto current = list_.begin(); current != list_.end(); ++current) {
            if (current->try_acquire()) {
                thread_data.merge(*current);
                current->release();
            }
        }
        scan();
    }

private:
    lu::thread_local_list<HazardThreadData> list_;
};

inline hazard_pointer_domain &get_default_domain() noexcept {
    static hazard_pointer_domain domain;
    return domain;
}

inline void attach_thread(hazard_pointer_domain &domain = get_default_domain()) {
    domain.attach_thread();
}

inline void detach_thread(hazard_pointer_domain &domain = get_default_domain()) {
    domain.detach_thread();
}

class hazard_pointer {
    using HazardObject = detail::HazardObject;
    using HazardRecord = detail::HazardRecord;

public:
    hazard_pointer() noexcept = default;

    explicit hazard_pointer(hazard_pointer_domain *domain) noexcept
        : domain_(domain)
        , record_(domain_->acquire_record()) {}

    hazard_pointer(const hazard_pointer &) = delete;

    hazard_pointer(hazard_pointer &&other) noexcept
        : domain_(other.domain_)
        , record_(other.record_) {
        other.record_ = {};
    }

    hazard_pointer &operator=(const hazard_pointer &) = delete;

    hazard_pointer &operator=(hazard_pointer &&other) noexcept {
        hazard_pointer temp(std::move(other));
        swap(temp);
        return *this;
    }

    ~hazard_pointer() {
        if (record_) [[likely]] {
            record_->reset();
            domain_->release_record(record_);
        }
    }

public:
    bool empty() const noexcept { return !record_; }

    explicit operator bool() const noexcept { return !empty(); }

    template <class Ptr>
    Ptr protect(const std::atomic<Ptr> &src) noexcept {
        return protect(src, [](auto &&p) { return std::forward<decltype(p)>(p); });
    }

    template <class Ptr, class Func>
    Ptr protect(const std::atomic<Ptr> &src, Func &&func) noexcept {
        auto ptr = src.load(std::memory_order_relaxed);
        while (!try_protect(ptr, src, std::forward<Func>(func))) {
        }
        return ptr;
    }

    template <class Ptr>
    bool try_protect(Ptr &ptr, const std::atomic<Ptr> &src) noexcept {
        return try_protect(ptr, src, [](auto &&p) { return std::forward<decltype(p)>(p); });
    }

    template <class Ptr, class Func>
    bool try_protect(Ptr &ptr, const std::atomic<Ptr> &src, Func &&func) noexcept {
        assert(!empty() && "hazard_ptr must be initialized");
        auto old = ptr;
        reset_protection(func(old));
        ptr = src.load(std::memory_order_acquire);
        if (old != ptr) {
            reset_protection();
            return false;
        }
        return true;
    }

    template <class Ptr>
    void reset_protection(const Ptr ptr) noexcept {
        assert(!empty() && "hazard_ptr must be initialized");
        if constexpr (std::is_base_of_v<HazardObject, typename std::pointer_traits<Ptr>::element_type>) {
            record_->reset(static_cast<const HazardObject *>(to_raw_pointer(ptr)));
        } else {// if non intrusive hazard obj
            record_->reset(to_raw_pointer(ptr));
        }
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void reset_protection(std::nullptr_t = nullptr) noexcept {
        assert(!empty() && "hazard_ptr must be initialized");
        record_->reset();
    }

    void swap(hazard_pointer &other) noexcept {
        std::swap(domain_, other.domain_);
        std::swap(record_, other.record_);
    }

    friend void swap(hazard_pointer &left, hazard_pointer &right) noexcept { left.swap(right); }

private:
    hazard_pointer_domain *domain_{};
    HazardRecord *record_{};
};

inline hazard_pointer make_hazard_pointer(hazard_pointer_domain &domain = get_default_domain()) {
    return hazard_pointer(&domain);
}

template <class ValueType>
class guarded_ptr {
public:
    using element_type = ValueType;

    using pointer = element_type *;
    using reference = element_type &;
    using const_reference = const element_type &;

public:
    guarded_ptr() noexcept = default;

    guarded_ptr(hazard_pointer guard, pointer ptr) noexcept
        : guard_(std::move(guard))
        , ptr_(ptr) {}

    guarded_ptr(const guarded_ptr &) = delete;

    guarded_ptr &operator=(const guarded_ptr &) = delete;

    pointer operator->() const noexcept { return ptr_; }

    reference operator*() noexcept { return *ptr_; }

    const_reference operator*() const noexcept { return *ptr_; }

    explicit operator bool() const noexcept { return ptr_; }

    std::pair<hazard_pointer, pointer> unpack() && noexcept { return {std::move(guard_), ptr_}; }

private:
    hazard_pointer guard_{};
    pointer ptr_{};
};

namespace detail {

template <class ValueType, class Deleter, bool = std::is_empty_v<Deleter> && !std::is_final_v<Deleter>>
class HazardPointerDeleter;

template <class ValueType, class Deleter>
class HazardPointerDeleter<ValueType, Deleter, false> {
protected:
    void set_deleter(Deleter deleter) noexcept(std::is_nothrow_move_assignable_v<Deleter>) {
        deleter_ = std::move(deleter);
    }

    void do_delete(ValueType *value) noexcept { deleter_(value); }

private:
    Deleter deleter_;
};

template <class ValueType, class Deleter>
class HazardPointerDeleter<ValueType, Deleter, true> : private Deleter {
protected:
    void set_deleter(Deleter deleter) noexcept(std::is_nothrow_move_assignable_v<Deleter>) {
        Deleter::operator=(std::move(deleter));
    }

    void do_delete(ValueType *value) noexcept { Deleter::operator()(value); }
};

}// namespace detail

template <class ValueType, class Deleter = std::default_delete<ValueType>>
class hazard_pointer_obj_base : public detail::HazardObject,
                                private detail::HazardPointerDeleter<ValueType, Deleter> {
protected:
    hazard_pointer_obj_base() noexcept = default;

    hazard_pointer_obj_base(const hazard_pointer_obj_base &) noexcept = default;

    hazard_pointer_obj_base(hazard_pointer_obj_base &&) noexcept = default;

public:
    void retire(Deleter deleter, hazard_pointer_domain &domain = get_default_domain()) noexcept {
        assert(!retired_.exchange(true, std::memory_order_relaxed) && "Double retire is not allowed");
        this->set_deleter(std::move(deleter));
        this->set_reclaim(reclaim_func);
        this->set_key(static_cast<HazardObject *>(this));
        domain.retire(this);
    }

    void retire(hazard_pointer_domain &domain = get_default_domain()) noexcept { retire({}, domain); }

private:
    static void reclaim_func(HazardObject *obj) noexcept {
        auto obj_base = static_cast<hazard_pointer_obj_base *>(obj);
        auto value = static_cast<ValueType *>(obj);
        obj_base->do_delete(value);
    }

#ifndef NDEBUG
private:
    std::atomic<bool> retired_{false};
#endif
};

}// namespace lu

#endif
