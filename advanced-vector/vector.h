#pragma once

#include <algorithm>
#include <iterator>
#include <cassert>
#include <memory>
#include <new>
#include <type_traits>
#include <stdexcept>
#include <utility>


template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;

    RawMemory(RawMemory&& other) noexcept
        : buffer_(std::exchange(other.buffer_, nullptr)),
          capacity_(std::exchange(other.capacity_, 0)) {}

    RawMemory& operator=(const RawMemory&) = delete;

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }

        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    operator T*() noexcept {
        return buffer_;
    }

    operator const T*() const noexcept {
        return buffer_;
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n > 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() = default;

    explicit Vector(size_t size) : data_(size), size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    explicit Vector(size_t size, const T& value)
    : data_(size), size_(size) {
        std::uninitialized_fill_n(data_.GetAddress(), size, value);
    }

    Vector(std::initializer_list<T> values)
    : data_(values.size()), size_(data_.Capacity()) {
        std::uninitialized_copy(values.begin(), values.end(), data_.GetAddress());
    }

    template <typename Iter>
    explicit Vector(Iter first, Iter last)
    : data_(std::distance(first, last)), size_(data_.Capacity()) {
        std::uninitialized_copy(first, last, data_.GetAddress());
    }

    Vector(const Vector& other) : data_(other.size_), size_(other.size_) {
        std::uninitialized_copy(other.begin(), other.end(), data_.GetAddress());
    }

    Vector(Vector&& other)
        : data_(std::exchange(other.data_, {})),
          size_(std::exchange(other.size_, 0)) {}

    ~Vector() noexcept {
        if (data_.GetAddress()) {
            std::destroy_n(data_.GetAddress(), size_);
        }
    }

    Vector& operator=(const Vector& rhs) {
        if (this == &rhs) {
            return *this;
        }

        if (Capacity() >= rhs.size_) {
            // До этой позиции будет присваивание, а после - разрушение или инициализация
            size_t min_size = std::min(size_, rhs.size_);
            std::copy_n(rhs.begin(), min_size, begin());

            if (size_ > rhs.size_) {
                std::destroy(begin() + min_size, begin() + size_);
            } else {
                std::uninitialized_copy(rhs.begin() + min_size, rhs.end(), begin() + min_size);
            }
            size_ = rhs.size_;
        } else {
            Vector tmp(rhs);
            Swap(tmp);
        }

        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }

        return *this;
    }

    iterator begin() noexcept {
        return data_;
    }

    const_iterator begin() const noexcept {
        return data_;
    }

    const_iterator cbegin() const noexcept {
        return begin();
    }

    iterator end() noexcept {
        return data_ + size_;
    }

    const_iterator end() const noexcept {
        return data_ + size_;
    }

    const_iterator cend() const noexcept {
        return end();
    }

    T& Front() noexcept {
        return *begin();
    }

    const T& Front() const noexcept {
        return *begin();
    }

    T& Back() noexcept {
        return *std::prev(end());
    }

    const T& Back() const noexcept {
        return *std::prev(end());
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);
    
        OverwriteData(begin(), end(), new_data);

        std::destroy_n(begin(), size_);
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        // Уменьшаем размер
        if (size_ > new_size) {
            std::destroy(begin() + new_size, end());
            size_ = new_size;
            return;
        }

        // Размер надо увеличить, но увеличение начинаем с проверки capacity
        if (new_size > Capacity()) {
            RawMemory<T> new_data(new_size);
            std::uninitialized_value_construct(new_data + size_, new_data + new_size);
            
            OverwriteData(begin(), end(), new_data);

            std::destroy(begin(), end());
            data_.Swap(new_data);
        } else {
            std::uninitialized_value_construct(end(), begin() + new_size);
        }
        size_ = new_size;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(end(), std::forward<Args>(args)...);
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    void PopBack() {
        assert(size_ != 0);

        std::destroy_at(&Back());
        --size_;
    }

    template <typename... Args>
    iterator Emplace(const_iterator it, Args&& ... args) {
        assert(begin() <= it && it <= end());
        // Метод должен принимать константные и неконстантные итераторы, но для работы метода итератор должен быть неконстантным
        iterator non_const_it = const_cast<iterator>(it);
        // Позиция, в которой будет произведена вставка
        size_t it_pos = std::distance(begin(), non_const_it);

        if (Capacity() == Size()) { // Нужна реаллокация
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            iterator it_in_new_data = std::next(new_data.GetAddress(), it_pos);
            
            std::construct_at(it_in_new_data, std::forward<Args>(args)...);
            
            OverwriteData(begin(), non_const_it, new_data.GetAddress());
            OverwriteData(non_const_it, end(), it_in_new_data + 1);
            
            std::destroy(begin(), end());
            data_.Swap(new_data);
        } else { // Реаллокация не нужна, памяти хватает
            if (it_pos == size_) {
                // Итератор указывает на end(), ничего перемещать не надо, достаточно 1 раз вызвать конструктор
                std::construct_at(end(), std::forward<Args>(args)...);
            } else { // Нужны перемещения/копирования и 1 присваивание
                T new_value(std::forward<Args>(args)...);

                if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                    std::construct_at(end(), std::move(Back()));
                } else {
                    std::construct_at(end(), Back());
                }

                std::move_backward(non_const_it, std::prev(end()), end());
                *non_const_it = std::move(new_value);
            }
        }
        ++size_;

        return std::next(begin(), it_pos);
    }

    iterator Insert(const_iterator it, const T& val) {
        return Emplace(it, val);
    }

    iterator Insert(const_iterator it, T&& val) {
        return Emplace(it, std::move(val));
    }

    iterator Erase(const_iterator it) {
        assert(begin() <= it && it < end());
 
        iterator non_const_it = const_cast<iterator>(it);

        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::move(std::next(non_const_it), end(), non_const_it);
        } else {
            std::copy(std::next(non_const_it), end(), non_const_it);
        }

        std::destroy_at(&Back());
        --size_;

        return non_const_it;
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    void Swap(Vector& rhs) noexcept {
        std::swap(data_, rhs.data_);
        std::swap(size_, rhs.size_);
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;


    // Перемещает/копирует данные из одного отрезка памяти в другой такого же диапазона (часто в коде нужна операция, метод для избежания дублирования)
    static void OverwriteData(iterator InpFirst, iterator InpLast, iterator DestIter) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move(InpFirst, InpLast, DestIter);
        } else {
            std::uninitialized_copy(InpFirst, InpLast, DestIter);
        }
    }
};

template <typename T>
Vector(size_t, T) -> Vector<T>;

template <typename T>
Vector(std::initializer_list<T>) -> Vector<T>;

template <typename Iter>
Vector(Iter, Iter) -> Vector<typename std::iterator_traits<Iter>::value_type>;