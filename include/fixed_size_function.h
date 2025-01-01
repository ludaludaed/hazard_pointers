#ifndef __FIXED_SIZE_FUNCTION_H__
#define __FIXED_SIZE_FUNCTION_H__

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>


namespace lu {

template<class ResultType, class... Args>
struct FixedSizeFunctionVTable {
    using call_func_ptr = ResultType (*)(void *, Args &&...);
    using destruct_func_ptr = void (*)(void *);
    using copy_func_ptr = void (*)(void *, const void *);
    using move_func_ptr = void (*)(void *, void *);

    call_func_ptr call{};
    destruct_func_ptr destruct{};
    copy_func_ptr copy{};
    move_func_ptr move{};
};

template<class, std::size_t>
class FixedSizeFunction;

template<class ResultType, class... Args, std::size_t BufferLen>
class FixedSizeFunction<ResultType(Args...), BufferLen> {
    using storage = unsigned char[BufferLen];
    using vtable = FixedSizeFunctionVTable<ResultType, Args...>;

public:
    using result_type = ResultType;

public:
    FixedSizeFunction() noexcept = default;

    template<class Functor, class = std::enable_if_t<sizeof(Functor) <= BufferLen>>
    FixedSizeFunction(Functor &&func) {
        Construct(std::forward<Functor>(func));
    }

    FixedSizeFunction(std::nullptr_t) noexcept {}

    FixedSizeFunction(const FixedSizeFunction &other) {
        Copy(other);
    }

    FixedSizeFunction(FixedSizeFunction &&other) {
        Move(std::move(other));
    }

    ~FixedSizeFunction() {
        Destruct();
    }

    FixedSizeFunction &operator=(const FixedSizeFunction &other) {
        Destruct();
        Copy(other);
        return *this;
    }

    FixedSizeFunction &operator=(FixedSizeFunction &&other) {
        Destruct();
        Move(std::move(other));
        return *this;
    }

    FixedSizeFunction &operator=(std::nullptr_t) {
        Destruct();
        return *this;
    }

    template<class Functor, class = std::enable_if_t<sizeof(Functor) <= BufferLen>>
    FixedSizeFunction &operator=(Functor &&func) {
        Destruct();
        Construct(std::forward<Functor>(func));
        return *this;
    }

    void swap(FixedSizeFunction &other) {
        FixedSizeFunction temp(other);
        other = std::move(*this);
        *this = std::move(temp);
    }

    friend void swap(FixedSizeFunction &left, FixedSizeFunction &right) {
        left.swap(right);
    }

    explicit operator bool() const noexcept {
        return table_.call;
    }

    ResultType operator()(Args &&...args) const {
        if (table_.call) {
            return table_.call(const_cast<storage *>(&data_), std::forward<Args>(args)...);
        }
        throw std::bad_function_call();
    }

    friend bool operator==(const FixedSizeFunction &left, std::nullptr_t right) {
        return !left;
    }

    friend bool operator==(std::nullptr_t left, const FixedSizeFunction &right) {
        return !right;
    }

    friend bool operator!=(const FixedSizeFunction &left, std::nullptr_t right) {
        return !(left == right);
    }

    friend bool operator!=(std::nullptr_t left, const FixedSizeFunction &right) {
        return !(left == right);
    }

private:
    template<class Functor, class = std::enable_if_t<sizeof(Functor) <= BufferLen>>
    void Construct(Functor &&func) {
        using functor_type = typename std::decay_t<Functor>;
        new (&data_) functor_type(std::forward<Functor>(func));

        table_.call = Call<functor_type>;
        table_.destruct = Destruct<functor_type>;
        if constexpr (std::is_copy_constructible_v<functor_type>) {
            table_.copy = CopyConstruct<functor_type>;
        }
        if constexpr (std::is_move_constructible_v<functor_type>) {
            table_.move = MoveConstruct<functor_type>;
        }
    }

    void Copy(const FixedSizeFunction &other) {
        if (other.table_.copy) {
            other.table_.copy(&data_, &other);
            table_ = other.table_;
        }
    }

    void Move(FixedSizeFunction &&other) {
        if (other.table_.move) {
            other.table_.move(&data_, &other);
            table_ = other.table_;
            other.Destruct();
        } else if (other.table_.copy) {
            Copy(other);
        }
    }

    void Destruct() {
        if (table_.destruct) {
            table_.destruct(&data_);
            table_ = vtable();
        }
    }

    template<class Functor>
    static void CopyConstruct(void *this_ptr, const void *other) {
        new (this_ptr) Functor(*reinterpret_cast<const Functor *>(other));
    }

    template<class Functor>
    static void MoveConstruct(void *this_ptr, void *other) {
        new (this_ptr) Functor(std::move(*reinterpret_cast<Functor *>(other)));
    }

    template<class Functor>
    static void Destruct(void *this_ptr) {
        reinterpret_cast<Functor *>(this_ptr)->~Functor();
    }

    template<class Functor>
    static ResultType Call(void *this_ptr, Args &&...args) {
        return reinterpret_cast<Functor *>(this_ptr)->operator()(std::forward<Args>(args)...);
    }

private:
    vtable table_;
    storage data_;
};

template<class Signature, std::size_t BufferLen>
using fixed_size_function = FixedSizeFunction<Signature, BufferLen>;

}// namespace lu

#endif