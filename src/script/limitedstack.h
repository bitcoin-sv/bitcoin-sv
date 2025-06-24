// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_SCRIPT_LIMITEDSTACK_H
#define BITCOIN_SCRIPT_LIMITEDSTACK_H

#include <concepts>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <vector>

typedef std::vector<uint8_t> valtype;

class stack_overflow_error : public std::overflow_error
{
public:
    explicit stack_overflow_error(const std::string& str)
        : std::overflow_error(str)
    {
    }
};

class LimitedStack;

class LimitedVector
{
private:
    valtype stackElement;
    std::reference_wrapper<LimitedStack> stack;

    LimitedVector(valtype stackElementIn, LimitedStack& stackIn);

    // WARNING: modifying returned element will NOT adjust stack size
    valtype& GetElementNonConst();
public:

    using difference_type = valtype::difference_type;

    // Memory usage of one stack element (without data). This is a consensus rule. Do not change.
    // It prevents someone from creating stack with millions of empty elements.
    static constexpr unsigned int ELEMENT_OVERHEAD = 32;

    // Warning: returned reference is invalidated if parent stack is modified.
    const valtype& GetElement() const;
    uint8_t& front();
    uint8_t& back();
    const uint8_t& front() const;
    const uint8_t& back() const;
    uint8_t& operator[](uint64_t pos);
    const uint8_t& operator[](uint64_t pos) const;

    size_t size() const;
    bool empty() const;

    void push_back(uint8_t element);
    void append(const LimitedVector& second);
    void padRight(size_t size, uint8_t signbit);

    std::vector<uint8_t>::iterator begin();
    std::vector<uint8_t>::iterator end();

    const std::vector<uint8_t>::const_iterator begin() const;
    const std::vector<uint8_t>::const_iterator end() const;

    bool MinimallyEncode();
    bool IsMinimallyEncoded(uint64_t maxSize) const;

    const LimitedStack& getStack() const;

    void shrink(difference_type start,
                difference_type length);

    friend class LimitedStack;
};

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class LimitedStack
{
public:
    using stack_type = std::vector<LimitedVector>;
    using size_type = stack_type::size_type;

private:
    uint64_t combinedStackSize = 0;
    uint64_t maxStackSize = 0;
    stack_type stack_;
    LimitedStack* parentStack { nullptr };
    void decreaseCombinedStackSize(uint64_t additionalSize);
    void increaseCombinedStackSize(uint64_t additionalSize);

    LimitedStack(const LimitedStack&) = default;
    LimitedStack() = default;

public:
    LimitedStack(uint64_t maxStackSizeIn);
    LimitedStack(std::vector<valtype> stackElements, uint64_t maxStackSizeIn);

    LimitedStack(LimitedStack&&) = default;
    LimitedStack& operator=(LimitedStack&&) = default;
    LimitedStack& operator=(const LimitedStack&) = delete;

    // Compares the stacks but ignores the parent.
    bool operator==(const LimitedStack& other) const;

    // Warning: returned reference is invalidated if stack is modified.
    LimitedVector& stacktop(int index);

    const LimitedVector& front() const;
    const LimitedVector& back() const;
    const LimitedVector& at(uint64_t i) const;

    uint64_t getCombinedStackSize() const;
    size_t size() const;
    bool empty() const;

    void pop_back();
    template<typename T>
        requires(std::same_as<std::remove_cvref_t<T>, LimitedVector>)
    void push_back(T&&);
    template<typename T>
        requires(std::same_as<std::remove_cvref_t<T>, valtype>)
    void push_back(T&&);
	void push_back(const std::initializer_list<uint8_t>&);

    // erase elements from including (top - first). element until excluding (top - last). element
    // first and last should be negative numbers (distance from the top)
    void erase(int first, int last);

    // index should be negative number (distance from the top)
    void erase(int index);

    // position should be negative number (distance from the top)
    void insert(int position, const LimitedVector& element);

    void swapElements(size_t index1, size_t index2);

    void moveTopToStack(LimitedStack& otherStack);

    void MoveToValtypes(std::vector<valtype>& script);

    LimitedStack makeChildStack();

    // parent must be null
    LimitedStack makeRootStackCopy();

    const LimitedStack* getParentStack() const;

    friend class LimitedVector;
};

template<typename T>
    requires(std::same_as<std::remove_cvref_t<T>, LimitedVector>)
void LimitedStack::push_back(T&& element)
{
    if(&element.getStack() != this)
    {
        throw std::invalid_argument(
            "Invalid argument - element that is added should have the same "
            "parent stack as the one we are adding to.");
    }
    increaseCombinedStackSize(element.size() + LimitedVector::ELEMENT_OVERHEAD);
    stack_.push_back(std::forward<T>(element));
}

template<typename T>
    requires(std::same_as<std::remove_cvref_t<T>, valtype>)
void LimitedStack::push_back(T&& element)
{
    increaseCombinedStackSize(element.size() + LimitedVector::ELEMENT_OVERHEAD);
    stack_.push_back(LimitedVector{std::forward<T>(element), *this});
}

#endif
