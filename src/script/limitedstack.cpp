// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "script/limitedstack.h"
#include "crypto/ripemd160.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "script/int_serialization.h"
#include "hash.h"
#include <iostream>

LimitedVector::LimitedVector(const valtype& stackElementIn, LimitedStack& stackIn) : stackElement(stackElementIn), stack(stackIn)
{
}

const valtype& LimitedVector::GetElement() const
{
    return stackElement;
}

valtype& LimitedVector::GetElementNonConst()
{
    return stackElement;
}

size_t LimitedVector::size() const
{
    return stackElement.size();
}

bool LimitedVector::empty() const
{
    return stackElement.empty();
}

uint8_t& LimitedVector::operator[](uint64_t pos)
{
    return stackElement[pos];
}

const uint8_t& LimitedVector::operator[](uint64_t pos) const
{
    return stackElement[pos];
}

void LimitedVector::push_back(uint8_t element)
{
    stack.get().increaseCombinedStackSize(1);
    stackElement.push_back(element);
}

void LimitedVector::append(const LimitedVector& second)
{
    stack.get().increaseCombinedStackSize(second.size());
    stackElement.insert(stackElement.end(), second.begin(), second.end());
}

void LimitedVector::padRight(size_t size, uint8_t signbit)
{
    if (size > stackElement.size())
    {
        size_t sizeDifference = size - stackElement.size();

        stack.get().increaseCombinedStackSize(sizeDifference);

        stackElement.resize(size, 0x00);
        stackElement.back() = signbit;
    }
}

valtype::iterator LimitedVector::begin()
{
    return stackElement.begin();
}

valtype::iterator LimitedVector::end()
{
    return stackElement.end();
}

const valtype::const_iterator LimitedVector::begin() const
{
    return stackElement.begin();
}

const valtype::const_iterator LimitedVector::end() const
{
    return stackElement.end();
}

uint8_t& LimitedVector::front()
{
    return stackElement.front();
}

uint8_t& LimitedVector::back()
{
    return stackElement.back();
}

const uint8_t& LimitedVector::front() const
{
    return stackElement.front();
}

const uint8_t& LimitedVector::back() const
{
    return stackElement.back();
}

bool LimitedVector::MinimallyEncode()
{
    stack.get().decreaseCombinedStackSize(stackElement.size());
    bool successfulEncoding = bsv::MinimallyEncode(stackElement);
    stack.get().increaseCombinedStackSize(stackElement.size());

    return successfulEncoding;
}

bool LimitedVector::IsMinimallyEncoded(uint64_t maxSize) const
{
    return bsv::IsMinimallyEncoded(stackElement, maxSize);
}
const LimitedStack& LimitedVector::getStack() const
{
    return stack.get();
}

LimitedStack::LimitedStack(uint64_t maxStackSizeIn)
{
    maxStackSize = maxStackSizeIn;
    parentStack = nullptr;
}

LimitedStack::LimitedStack(const std::vector<valtype>& stackElements, uint64_t maxStackSizeIn)
{
    maxStackSize = maxStackSizeIn;
    parentStack = nullptr;
    for (const auto& element : stackElements)
    {
        push_back(element);
    }
}

bool LimitedStack::operator==(const LimitedStack& other) const
{
    if (stack.size() != other.size())
    {
        return false;
    }

    for (size_t i = 0; i < stack.size(); i++)
    {
        if (stack.at(i).GetElement() != other.at(i).GetElement())
        {
            return false;
        }
    }

    return true;
}

void LimitedStack::decreaseCombinedStackSize(uint64_t additionalSize)
{
    if (parentStack != nullptr)
    {
        parentStack->decreaseCombinedStackSize(additionalSize);
    }
    else
    {
        combinedStackSize -= additionalSize;
    }
}

void LimitedStack::increaseCombinedStackSize(uint64_t additionalSize)
{
    if (parentStack != nullptr)
    {
        parentStack->increaseCombinedStackSize(additionalSize);
    }
    else
    {
        if (getCombinedStackSize() + additionalSize > maxStackSize)
        {
            throw stack_overflow_error("pushstack(): stack oversized");
        }

        combinedStackSize += additionalSize;
    }
}

void LimitedStack::pop_back()
{
    if (stack.empty())
    {
        throw std::runtime_error("popstack(): stack empty");
    }
    decreaseCombinedStackSize(stacktop(-1).size() + LimitedVector::ELEMENT_OVERHEAD);
    stack.pop_back();
}

void LimitedStack::push_back(const LimitedVector &element)
{
    if (&element.getStack() != this)
    {
        throw std::invalid_argument("Invalid argument - element that is added should have the same parent stack as the one we are adding to.");
    }
    increaseCombinedStackSize(element.size() + LimitedVector::ELEMENT_OVERHEAD);
    stack.push_back(element);
}

void LimitedStack::push_back(const valtype& element)
{
    increaseCombinedStackSize(element.size() + LimitedVector::ELEMENT_OVERHEAD);
    stack.push_back(LimitedVector{element, *this});
}

LimitedVector& LimitedStack::stacktop(int index)
{
    if (index >= 0)
    {
        throw std::invalid_argument("Invalid argument - index should be < 0.");
    };
    return stack.at(stack.size() + (index));
}

uint64_t LimitedStack::getCombinedStackSize() const
{
    if (parentStack != nullptr)
    {
        return parentStack->getCombinedStackSize();
    }

    return combinedStackSize;
}

void LimitedStack::erase(int first, int last)
{
    if (last >= 0 || last <= first)
    {
        throw std::invalid_argument("Invalid argument - first and last should be negative, also last should be larger than first.");
    }
    for (std::vector<LimitedVector>::iterator it = stack.end() + first; it != stack.end() + last; it++)
    {
        decreaseCombinedStackSize(it->size() + LimitedVector::ELEMENT_OVERHEAD);
    }

    stack.erase(stack.end() + first, stack.end() + last);
}

void LimitedStack::erase(int index)
{
    if (index >= 0)
    {
        throw std::invalid_argument("Invalid argument - index should be < 0.");
    };
    decreaseCombinedStackSize(stack.at(stack.size() + index).size() + LimitedVector::ELEMENT_OVERHEAD);
    stack.erase(stack.end() + index); 
}

void LimitedStack::insert(int position, const LimitedVector& element)
{
    if (&element.getStack() != this)
    {
        throw std::invalid_argument("Invalid argument - element that is added should have the same parent stack as the one we are adding to.");
    }

    if (position >= 0)
    {
        throw std::invalid_argument("Invalid argument - position should be < 0.");
    };
    increaseCombinedStackSize(element.size() + LimitedVector::ELEMENT_OVERHEAD);
    stack.insert(stack.end() + position, element);
}

void LimitedStack::swapElements(size_t index1, size_t index2)
{
    std::swap(stack.at(index1), stack.at(index2));
}

// this method does not change combinedSize
// it is allowed only for relations parent-child
void LimitedStack::moveTopToStack(LimitedStack& otherStack)
{
    if (parentStack == &otherStack || otherStack.getParentStack() == this)
    {
        // Moving element to other stack does not change the total size of stack.
        // Just use internal functions to move the element.
        stack.push_back(std::move(otherStack.stacktop(-1)));
        otherStack.stack.pop_back();
    }
    else
    {
        throw std::runtime_error("Method moveTopToStack is allowed only for relations parent-child.");
    }
}

size_t LimitedStack::size() const
{
    return stack.size();
}

const LimitedVector& LimitedStack::front() const
{
    return stack.front();
}

const LimitedVector& LimitedStack::back() const
{
    return stack.back();
}

const LimitedVector& LimitedStack::at(uint64_t i) const
{
    return stack.at(i);
}

bool LimitedStack::empty() const
{
    return stack.empty();
}

void  LimitedStack::MoveToValtypes(std::vector<valtype>& valtypes)
{
    for (LimitedVector& it : stack)
    {
        decreaseCombinedStackSize(it.size() + LimitedVector::ELEMENT_OVERHEAD);
        valtypes.push_back(std::move(it.GetElementNonConst()));
    }

    stack.clear();
}

LimitedStack LimitedStack::makeChildStack()
{
    LimitedStack stack;
    stack.parentStack = this;

    return stack;
}

LimitedStack LimitedStack::makeRootStackCopy()
{
    if (parentStack != nullptr)
    {
        throw std::runtime_error("Parent stack must be null if you are creating stack copy.");
    }

    return *this;
}

const LimitedStack* LimitedStack::getParentStack() const
{
    return parentStack;
}
