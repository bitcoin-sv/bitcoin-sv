// Copyright (c) 2015-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _BITCOIN_PREVECTOR_H_
// NOLINTNEXTLINE(bugprone-reserved-identifier)
#define _BITCOIN_PREVECTOR_H_

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <iterator>

#include "compiler_warnings.h"

#ifndef __clang__
    // -Wmaybe-uninitialized warning flag is disabled here because it gives false alarms in this class due to _union.indirect variable.
    // _union.indirect is always used after _size becomes larger than N. When this happens, _union.indirect is set for the first time.
    GCC_WARNINGS_IGNORE(-Wmaybe-uninitialized);
    #ifdef __GNUC__
        #if __GNUC__ >= 12
        // GCC 12 also includes this warning under -Wuninitialized
        GCC_WARNINGS_IGNORE(-Wuninitialized)
        #endif
    #endif
#endif

/**
 * Implements a drop-in replacement for std::vector<T> which stores up to N
 * elements directly (without heap allocation). The types Size and Diff are used
 * to store element counts, and can be any unsigned + signed type.
 *
 * Storage layout is either:
 * - Direct allocation:
 *   - Size _size: the number of used elements (between 0 and N)
 *   - T direct[N]: an array of N elements of type T
 *     (only the first _size are initialized).
 * - Indirect allocation:
 *   - Size _size: the number of used elements plus N + 1
 *   - Size capacity: the number of allocated elements
 *   - T* indirect: a pointer to an array of capacity elements of type T
 *     (only the first _size are initialized).
 *
 * The data type T must be movable by memmove/realloc(). Once we switch to C++,
 * move constructors can be used instead.
 */
// NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
template <unsigned int N, typename T, typename Size = uint64_t,
          typename Diff = int64_t>
class prevector {
public:
    typedef Size size_type;
    typedef Diff difference_type;
    typedef T value_type;
    typedef value_type &reference;
    typedef const value_type &const_reference;
    typedef value_type *pointer;
    typedef const value_type *const_pointer;

    class iterator {
        T *ptr;

    public:
        typedef Diff difference_type;
        using element_type = T;
        typedef T *pointer;
        typedef T &reference;
        using iterator_category = std::contiguous_iterator_tag;
        iterator() : ptr(nullptr) {}
        iterator(T *ptr_) : ptr(ptr_) {}
        T &operator*() const { return *ptr; }
        T *operator->() const { return ptr; }
        T &operator[](difference_type pos) { return ptr[pos]; }
        T& operator[](difference_type pos) const { return ptr[pos]; }

        iterator &operator++() {
            ptr++;
            return *this;
        }
        iterator &operator--() {
            ptr--;
            return *this;
        }
        iterator operator++(int) {
            iterator copy(*this);
            ++(*this);
            return copy;
        }
        iterator operator--(int) {
            iterator copy(*this);
            --(*this);
            return copy;
        }
        difference_type friend operator-(iterator a, iterator b) {
            return (&(*a) - &(*b));
        }
        iterator operator+(difference_type n) const { return iterator(ptr + n); }
        iterator& operator+=(difference_type n) {
            ptr += n;
            return *this;
        }
        iterator operator-(difference_type n) const { return iterator(ptr - n); }
        iterator& operator-=(difference_type n) {
            ptr -= n;
            return *this;
        }
        bool operator==(iterator x) const { return ptr == x.ptr; }
        bool operator!=(iterator x) const { return ptr != x.ptr; }
        bool operator>=(iterator x) const { return ptr >= x.ptr; }
        bool operator<=(iterator x) const { return ptr <= x.ptr; }
        bool operator>(iterator x) const { return ptr > x.ptr; }
        bool operator<(iterator x) const { return ptr < x.ptr; }

        friend iterator operator+(difference_type n, const iterator& it)
        {
            return iterator{it.ptr + n};
        }
  };
  static_assert(std::contiguous_iterator<iterator>);

    class reverse_iterator {
        T *ptr;

    public:
        typedef Diff difference_type;
        typedef T value_type;
        typedef T *pointer;
        typedef T &reference;
        typedef std::bidirectional_iterator_tag iterator_category;
        reverse_iterator() : ptr(nullptr) {}
        reverse_iterator(T *ptr_) : ptr(ptr_) {}
        T &operator*() { return *ptr; }
        const T &operator*() const { return *ptr; }
        T *operator->() { return ptr; }
        const T *operator->() const { return ptr; }
        reverse_iterator &operator--() {
            ptr++;
            return *this;
        }
        reverse_iterator &operator++() {
            ptr--;
            return *this;
        }
        reverse_iterator operator++(int) {
            reverse_iterator copy(*this);
            ++(*this);
            return copy;
        }
        reverse_iterator operator--(int) {
            reverse_iterator copy(*this);
            --(*this);
            return copy;
        }
        bool operator==(reverse_iterator x) const { return ptr == x.ptr; }
        bool operator!=(reverse_iterator x) const { return ptr != x.ptr; }
    };

    class const_iterator {
        const T *ptr;

    public:
        typedef Diff difference_type;
        using element_type = T;
        typedef const T *pointer;
        typedef const T &reference;
        using iterator_category = std::contiguous_iterator_tag; 
        const_iterator() : ptr(nullptr) {}
        const_iterator(const T *ptr_) : ptr(ptr_) {}
        const_iterator(iterator x) : ptr(&(*x)) {}
        const T &operator*() const { return *ptr; }
        const T *operator->() const { return ptr; }
        const T &operator[](size_type pos) const { return ptr[pos]; }
        const_iterator &operator++() {
            ptr++;
            return *this;
        }
        const_iterator &operator--() {
            ptr--;
            return *this;
        }
        const_iterator operator++(int) {
            const_iterator copy(*this);
            ++(*this);
            return copy;
        }
        const_iterator operator--(int) {
            const_iterator copy(*this);
            --(*this);
            return copy;
        }
        difference_type friend operator-(const_iterator a, const_iterator b) {
            return (&(*a) - &(*b));
        }
        const_iterator operator+(difference_type n) const {
            return const_iterator(ptr + n);
        }
        const_iterator &operator+=(difference_type n) {
            ptr += n;
            return *this;
        }
        const_iterator operator-(difference_type n) const {
            return const_iterator(ptr - n);
        }
        const_iterator &operator-=(difference_type n) {
            ptr -= n;
            return *this;
        }
        bool operator==(const_iterator x) const { return ptr == x.ptr; }
        bool operator!=(const_iterator x) const { return ptr != x.ptr; }
        bool operator>=(const_iterator x) const { return ptr >= x.ptr; }
        bool operator<=(const_iterator x) const { return ptr <= x.ptr; }
        bool operator>(const_iterator x) const { return ptr > x.ptr; }
        bool operator<(const_iterator x) const { return ptr < x.ptr; }

        friend const_iterator operator+(difference_type n, const const_iterator& it)
        {
            return const_iterator{it.ptr + n};
        }
    };
    static_assert(std::contiguous_iterator<const_iterator>);

    class const_reverse_iterator {
        const T *ptr;

    public:
        typedef Diff difference_type;
        typedef const T value_type;
        typedef const T *pointer;
        typedef const T &reference;
        typedef std::bidirectional_iterator_tag iterator_category;
        const_reverse_iterator() : ptr(nullptr) {}
        const_reverse_iterator(T *ptr_) : ptr(ptr_) {}
        const_reverse_iterator(reverse_iterator x) : ptr(&(*x)) {}
        const T &operator*() const { return *ptr; }
        const T *operator->() const { return ptr; }
        const_reverse_iterator &operator--() {
            ptr++;
            return *this;
        }
        const_reverse_iterator &operator++() {
            ptr--;
            return *this;
        }
        const_reverse_iterator operator++(int) {
            const_reverse_iterator copy(*this);
            ++(*this);
            return copy;
        }
        const_reverse_iterator operator--(int) {
            const_reverse_iterator copy(*this);
            --(*this);
            return copy;
        }
        bool operator==(const_reverse_iterator x) const { return ptr == x.ptr; }
        bool operator!=(const_reverse_iterator x) const { return ptr != x.ptr; }
    };

private:
    size_type _size;
    union direct_or_indirect {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
        char direct[sizeof(T) * N];
        struct {
            size_type capacity;
            char *indirect;
        } other;
    } _union;

    T *direct_ptr(difference_type pos) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<T *>(_union.direct) + pos;
    }
    const T *direct_ptr(difference_type pos) const {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<const T *>(_union.direct) + pos;
    }
    T *indirect_ptr(difference_type pos) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<T *>(_union.other.indirect) + pos;
    }
    const T *indirect_ptr(difference_type pos) const {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<const T *>(_union.other.indirect) + pos;
    }
    bool is_direct() const { return _size <= N; }

    void change_capacity(size_type new_capacity) {
        if (new_capacity <= N) {
            if (!is_direct()) {
                T *indirect = indirect_ptr(0);
                T *src = indirect;
                T *dst = direct_ptr(0);
                memcpy(dst, src, size() * sizeof(T));
                // NOLINTNEXTLINE(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
                free(indirect);
                _size -= N + 1;
            }
        } else {
            if (!is_direct()) {
                // FIXME: Because malloc/realloc here won't call new_handler if
                // allocation fails, assert success. These should instead use an
                // allocator or new/delete so that handlers are called as
                // necessary, but performance would be slightly degraded by
                // doing so.
                // NOLINTNEXTLINE(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
                _union.other.indirect = static_cast<char *>(realloc(
                    _union.other.indirect, ((size_t)sizeof(T)) * new_capacity));
                assert(_union.other.indirect);
                _union.other.capacity = new_capacity;
            } else {
                // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
                char *new_indirect = static_cast<char *>(
                    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
                    malloc(((size_t)sizeof(T)) * new_capacity));
                assert(new_indirect);
                T *src = direct_ptr(0);
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                T *dst = reinterpret_cast<T *>(new_indirect);
                memcpy(dst, src, size() * sizeof(T));
                _union.other.indirect = new_indirect;
                _union.other.capacity = new_capacity;
                _size += N + 1;
            }
        }
    }

    T *item_ptr(difference_type pos) {
        return is_direct() ? direct_ptr(pos) : indirect_ptr(pos);
    }
    const T *item_ptr(difference_type pos) const {
        return is_direct() ? direct_ptr(pos) : indirect_ptr(pos);
    }

public:
    void assign(size_type n, const T &val) {
        clear();
        if (capacity() < n) {
            change_capacity(n);
        }
        while (size() < n) {
            _size++;
            new (static_cast<void *>(item_ptr(static_cast<difference_type>(size() - 1)))) T(val);
        }
    }

    template <typename InputIterator>
    void assign(InputIterator first, InputIterator last) {
        size_type n = last - first;
        clear();
        if (capacity() < n) {
            change_capacity(n);
        }
        while (first != last) {
            _size++;
            new (static_cast<void *>(item_ptr(static_cast<difference_type>(size() - 1)))) T(*first);
            ++first;
        }
    }

    prevector() : _size(0) {}

    explicit prevector(size_type n) : _size(0) { resize(n); }

    explicit prevector(size_type n, const T &val = T()) : _size(0) {
        change_capacity(n);
        while (size() < n) {
            _size++;
            new (static_cast<void *>(item_ptr(static_cast<difference_type>(size() - 1)))) T(val);
        }
    }

    template <typename InputIterator>
    prevector(InputIterator first, InputIterator last) : _size(0) {
        size_type n = static_cast<size_type>(last - first);
        change_capacity(n);
        while (first != last) {
            _size++;
            new (static_cast<void *>(item_ptr(static_cast<difference_type>(size() - 1)))) T(*first);
            ++first;
        }
    }

    prevector(const prevector<N, T, Size, Diff> &other) : _size(0) {
        change_capacity(other.size());
        const_iterator it = other.begin();
        while (it != other.end()) {
            _size++;
            new (static_cast<void *>(item_ptr(static_cast<difference_type>(size() - 1)))) T(*it);
            ++it;
        }
    }

    // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations, performance-noexcept-move-constructor)
    prevector(prevector<N, T, Size, Diff> &&other) : _size(0) { swap(other); }

    prevector &operator=(const prevector<N, T, Size, Diff> &other) {
        if (&other == this) {
            return *this;
        }
        resize(0);
        change_capacity(other.size());
        const_iterator it = other.begin();
        while (it != other.end()) {
            _size++;
            new (static_cast<void *>(item_ptr(static_cast<difference_type>(size() - 1)))) T(*it);
            ++it;
        }
        return *this;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-noexcept-move-operations, performance-noexcept-move-constructor)
    prevector &operator=(prevector<N, T, Size, Diff> &&other) {
        swap(other);
        return *this;
    }

    size_type size() const { return is_direct() ? _size : _size - N - 1; }

    bool empty() const { return size() == 0; }

    iterator begin() { return iterator(item_ptr(0)); }
    const_iterator begin() const { return const_iterator(item_ptr(0)); }
    iterator end() { return iterator(item_ptr(static_cast<difference_type>(size()))); }
    const_iterator end() const { return const_iterator(item_ptr(static_cast<difference_type>(size()))); }

    reverse_iterator rbegin() { return reverse_iterator(item_ptr(static_cast<difference_type>(size() - 1))); }
    const_reverse_iterator rbegin() const {
        return const_reverse_iterator(item_ptr(static_cast<difference_type>(size() - 1)));
    }
    reverse_iterator rend() { return reverse_iterator(item_ptr(static_cast<difference_type>(-1))); }
    const_reverse_iterator rend() const {
        return const_reverse_iterator(item_ptr(static_cast<difference_type>(-1)));
    }

    size_t capacity() const {
        if (is_direct()) {
            return N;
        } else {
            return _union.other.capacity;
        }
    }

    T &operator[](size_type pos) { return *item_ptr(static_cast<difference_type>(pos)); }

    const T &operator[](size_type pos) const { return *item_ptr(static_cast<difference_type>(pos)); }

    void resize(size_type new_size) {
        if (size() > new_size) {
            erase(item_ptr(static_cast<difference_type>(new_size)), end());
        }
        if (new_size > capacity()) {
            change_capacity(new_size);
        }
        while (size() < new_size) {
            _size++;
            new (static_cast<void *>(item_ptr(static_cast<difference_type>(size() - 1)))) T();
        }
    }

    void reserve(size_type new_capacity) {
        if (new_capacity > capacity()) {
            change_capacity(new_capacity);
        }
    }

    void shrink_to_fit() { change_capacity(size()); }

    void clear() { resize(0); }

    iterator insert(iterator pos, const T &value) {
        size_type p = static_cast<size_type>(pos - begin());
        size_type new_size = size() + 1;
        if (capacity() < new_size) {
            change_capacity(new_size + (new_size >> 1));
        }
        memmove(item_ptr(static_cast<difference_type>(p + 1)), item_ptr(static_cast<difference_type>(p)), (size() - p) * sizeof(T));
        _size++;
        new (static_cast<void *>(item_ptr(static_cast<difference_type>(p)))) T(value);
        return iterator(item_ptr(static_cast<difference_type>(p)));
    }

    void insert(iterator pos, size_type count, const T &value) {
        size_type p = static_cast<size_type>(pos - begin());
        size_type new_size = size() + count;
        if (capacity() < new_size) {
            change_capacity(new_size + (new_size >> 1));
        }
        memmove(item_ptr(static_cast<difference_type>(p + count)), item_ptr(static_cast<difference_type>(p)), (size() - p) * sizeof(T));
        _size += count;
        for (size_type i = 0; i < count; i++) {
            new (static_cast<void *>(item_ptr(static_cast<difference_type>(p + i)))) T(value);
        }
    }

    template <typename InputIterator>
    void insert(iterator pos, InputIterator first, InputIterator last) {
        size_type p = static_cast<size_type>(pos - begin());
        size_type count = static_cast<size_type>(last - first);
        size_type new_size = size() + count;
        if (capacity() < new_size) {
            change_capacity(new_size + (new_size >> 1));
        }
        memmove(item_ptr(static_cast<difference_type>(p + count)), item_ptr(static_cast<difference_type>(p)), (size() - p) * sizeof(T));
        _size += count;
        while (first != last) {
            new (static_cast<void *>(item_ptr(static_cast<difference_type>(p)))) T(*first);
            ++p;
            ++first;
        }
    }

    iterator erase(iterator pos) { return erase(pos, pos + 1); }

    iterator erase(iterator first, iterator last) {
        iterator p = first;
        char *endp = (char *)&(*end());
        while (p != last) {
            (*p).~T();
            _size--;
            ++p;
        }
        memmove(&(*first), &(*last), static_cast<size_t>(endp - ((char *)(&(*last)))));
        return first;
    }

    void push_back(const T &value) {
        size_type new_size = size() + 1;
        if (capacity() < new_size) {
            change_capacity(new_size + (new_size >> 1));
        }
        new (item_ptr(static_cast<difference_type>(size()))) T(value);
        _size++;
    }

    void pop_back() { erase(end() - 1, end()); }

    T &front() { return *item_ptr(0); }

    const T &front() const { return *item_ptr(0); }

    T &back() { return *item_ptr(size() - 1); }

    const T &back() const { return *item_ptr(size() - 1); }

    // NOLINTNEXTLINE(cppcoreguidelines-noexcept-swap, performance-noexcept-swap)
    void swap(prevector<N, T, Size, Diff> &other) {
        std::swap(_union, other._union);
        std::swap(_size, other._size);
    }

    ~prevector() {
        clear();
        if (!is_direct()) {
            // NOLINTNEXTLINE(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
            free(_union.other.indirect);
            _union.other.indirect = nullptr;
        }
    }

    bool operator==(const prevector<N, T, Size, Diff> &other) const {
        if (other.size() != size()) {
            return false;
        }
        const_iterator b1 = begin();
        const_iterator b2 = other.begin();
        const_iterator e1 = end();
        while (b1 != e1) {
            if ((*b1) != (*b2)) {
                return false;
            }
            ++b1;
            ++b2;
        }
        return true;
    }

    bool operator!=(const prevector<N, T, Size, Diff> &other) const {
        return !(*this == other);
    }

    bool operator<(const prevector<N, T, Size, Diff> &other) const {
        if (size() < other.size()) {
            return true;
        }
        if (size() > other.size()) {
            return false;
        }
        const_iterator b1 = begin();
        const_iterator b2 = other.begin();
        const_iterator e1 = end();
        while (b1 != e1) {
            if ((*b1) < (*b2)) {
                return true;
            }
            if ((*b2) < (*b1)) {
                return false;
            }
            ++b1;
            ++b2;
        }
        return false;
    }

    size_t allocated_memory() const {
        if (is_direct()) {
            return 0;
        } else {
            return ((size_t)(sizeof(T))) * _union.other.capacity;
        }
    }

    value_type *data() { return item_ptr(0); }

    const value_type *data() const { return item_ptr(0); }
};
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
// NOLINTEND(cppcoreguidelines-pro-type-union-access)

#endif
