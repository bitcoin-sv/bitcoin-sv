// Copyright (c) 2018 The Bitcoin developers
// Copyright (c) 2018-2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "test/test_bitcoin.h"
#include "script/limitedstack.h"
#include <boost/test/unit_test.hpp>

typedef std::vector<uint8_t> valtype;

BOOST_FIXTURE_TEST_SUITE(limited_stack_vector_tests, BasicTestingSetup)


BOOST_AUTO_TEST_CASE(limitedstack_push_test) {
    ////////// LimitedStack Push back check //////////
    {
        valtype vtype;
        vtype.push_back(0xab);
        LimitedStack limitedStack(100);
        unsigned push_count = 3;

        // Push back
        for (unsigned i = 0; i < push_count; i++) {
            limitedStack.push_back(vtype);
        }

        BOOST_CHECK_EQUAL(limitedStack.size(), push_count);
        BOOST_CHECK_EQUAL(limitedStack.getCombinedStackSize(), (push_count*LimitedVector::ELEMENT_OVERHEAD) + 3);
        BOOST_CHECK_THROW(limitedStack.push_back(vtype), std::runtime_error);
    }
}

BOOST_AUTO_TEST_CASE(limitedstack_insert_test) {
    ////////// LimitedStack Insert check //////////
    {
        LimitedStack limitedStack(100);
        BOOST_CHECK_EQUAL(limitedStack.size(), 0U);

        limitedStack.push_back({0xab}); // There needs to be at least one element for insert method to work.

        BOOST_CHECK_EQUAL(limitedStack.size(), 1U);
        BOOST_CHECK_EQUAL(limitedStack.getCombinedStackSize(), limitedStack.size()*LimitedVector::ELEMENT_OVERHEAD + 1);

        LimitedVector limitedVector = limitedStack.stacktop(-1);

        // Insert
        limitedStack.insert(-1, limitedVector);
        limitedStack.insert(-1, limitedVector);

        BOOST_CHECK_EQUAL(limitedStack.size(), 3U);
        BOOST_CHECK_EQUAL(limitedStack.getCombinedStackSize(), (limitedStack.size()*LimitedVector::ELEMENT_OVERHEAD) + 3);
        BOOST_CHECK_THROW(limitedStack.insert(-1, limitedVector), std::runtime_error);
    }
}

BOOST_AUTO_TEST_CASE(limitedstack_erase_test) {
    ////////// LimitedStack erase(int first, int last) check //////////
    {
        LimitedStack limitedStack({{0xab}, {0xcd}, {0xef}, {0xab}}, 200);

        BOOST_CHECK_EQUAL(limitedStack.size(), 4U);

        limitedStack.erase(-3, -1);

        BOOST_CHECK_EQUAL(limitedStack.size(), 2U);
        BOOST_CHECK_EQUAL(limitedStack.at(0).GetElement().at(0), 0xab);
        BOOST_CHECK_EQUAL(limitedStack.at(1).GetElement().at(0), 0xab);
    }

    ////////// LimitedStack erase(int index) check //////////
    {
        LimitedStack limitedStack({{0xab}, {0xcd}, {0xef}, {0xab}}, 200);
        BOOST_CHECK_EQUAL(limitedStack.size(), 4U);
        limitedStack.erase(-3);

        BOOST_CHECK_EQUAL(limitedStack.size(), 3U);
        BOOST_CHECK_EQUAL(limitedStack.at(0).GetElement().at(0), 0xab);
        BOOST_CHECK_EQUAL(limitedStack.at(1).GetElement().at(0), 0xef);
        BOOST_CHECK_EQUAL(limitedStack.at(2).GetElement().at(0), 0xab);
    }
}

BOOST_AUTO_TEST_CASE(limitedstack_empty_test) {
    ////////// LimitedStack empty() check //////////
    {
        LimitedStack limitedStack(200);
        BOOST_CHECK_EQUAL(limitedStack.empty(), true);
        limitedStack.push_back({0xab});
        BOOST_CHECK_EQUAL(limitedStack.size(), 1U);
        BOOST_CHECK_EQUAL(limitedStack.empty(), false);
    }
}

BOOST_AUTO_TEST_CASE(limitedstack_op_sqbr_test) {
    ////////// LimitedStack operator[] check //////////
    {
        LimitedStack limitedStack({{0xab, 0xcd}}, 100);
        BOOST_CHECK_EQUAL(limitedStack.front()[0], 0xab);
        BOOST_CHECK_EQUAL(limitedStack.front()[1], 0xcd);
    }
}

BOOST_AUTO_TEST_CASE(limitedstack_front_back_test) {
    ////////// LimitedStack front() and back() check //////////
    {
        LimitedStack limitedStack(200);
        limitedStack.push_back({0xab, 0xcd});
        limitedStack.push_back({0xff, 0xfe});
        limitedStack.push_back({0xef, 0x12});
        BOOST_CHECK_EQUAL(limitedStack.front()[0], 0xab);
        BOOST_CHECK_EQUAL(limitedStack.front()[1], 0xcd);
        BOOST_CHECK_EQUAL(limitedStack.back()[0], 0xef);
        BOOST_CHECK_EQUAL(limitedStack.back()[1], 0x12);
    }
}

BOOST_AUTO_TEST_CASE(limitedstack_swap_test) {
    ////////// LimitedStack Swap check //////////
    {
        valtype vtype;
        LimitedStack limitedStack(100);

        BOOST_CHECK_EQUAL(limitedStack.size(), 0U);

        limitedStack.push_back({0xab});
        limitedStack.push_back({0xcd});
        uint64_t size_combined_before_swap = limitedStack.getCombinedStackSize();
        size_t size_before_swap = limitedStack.size();
        limitedStack.swapElements(0, 1);

        BOOST_CHECK_EQUAL(limitedStack.at(0).GetElement().at(0), 0xcd);
        BOOST_CHECK_EQUAL(limitedStack.at(1).GetElement().at(0), 0xab);
        BOOST_CHECK_EQUAL(size_combined_before_swap, limitedStack.getCombinedStackSize());
        BOOST_CHECK_EQUAL(size_before_swap, limitedStack.size());
    }
}

BOOST_AUTO_TEST_CASE(limitedstack_child_test) {
    ////////// LimitedStack Child check //////////
    {
        valtype vtype;
        LimitedStack limitedStack(100);
        LimitedStack limitedStack_child = limitedStack.makeChildStack();
        vtype.clear();
        vtype.push_back({0xab});
        limitedStack.push_back(vtype);

        // Check that combined size increases if parent is increased
        limitedStack.push_back(vtype);
        BOOST_CHECK_EQUAL(limitedStack.getCombinedStackSize(), 2 * (vtype.size() + LimitedVector::ELEMENT_OVERHEAD));
        BOOST_CHECK_EQUAL(limitedStack_child.getCombinedStackSize(), 2 * (vtype.size() + LimitedVector::ELEMENT_OVERHEAD));

        // Check that combined size increases if child is increased
        limitedStack_child.push_back(vtype);
        BOOST_CHECK_EQUAL(limitedStack.getCombinedStackSize(), 3 * (vtype.size() + LimitedVector::ELEMENT_OVERHEAD));
        BOOST_CHECK_EQUAL(limitedStack_child.getCombinedStackSize(), 3 * (vtype.size() + LimitedVector::ELEMENT_OVERHEAD));
    }
}

BOOST_AUTO_TEST_CASE(limitedstack_movetoptostack_test) {
    ////////// LimitedStack MoveTopToStack check //////////
    {
        LimitedStack limitedStack_parent(100);
        LimitedStack limitedStack_child = limitedStack_parent.makeChildStack();
        limitedStack_parent.push_back({0xab, 0xcd});
        limitedStack_child.push_back({0xef, 0x12});

        uint64_t size_child =  limitedStack_child.getCombinedStackSize();
        uint64_t size_parent = limitedStack_parent.getCombinedStackSize();

        limitedStack_child.moveTopToStack(limitedStack_parent);

        BOOST_CHECK_EQUAL(limitedStack_parent.getCombinedStackSize(), size_parent);
        BOOST_CHECK_EQUAL(limitedStack_parent.getCombinedStackSize(), size_child);
        BOOST_CHECK_EQUAL(limitedStack_child.getCombinedStackSize(), size_parent);
        BOOST_CHECK_EQUAL(limitedStack_child.getCombinedStackSize(), size_child);

        BOOST_CHECK_EQUAL(limitedStack_child.at(0).GetElement().at(0), 0xef);
        BOOST_CHECK_EQUAL(limitedStack_child.at(0).GetElement().at(1), 0x12);
        BOOST_CHECK_EQUAL(limitedStack_child.at(1).GetElement().at(0), 0xab);
        BOOST_CHECK_EQUAL(limitedStack_child.at(1).GetElement().at(1), 0xcd);

        BOOST_CHECK_EQUAL(limitedStack_child.getCombinedStackSize(), 2 * (limitedStack_child.size() + LimitedVector::ELEMENT_OVERHEAD));
        BOOST_CHECK_EQUAL(limitedStack_parent.size(), 0U);
    }
}

BOOST_AUTO_TEST_CASE(limitedstack_movetovaltypes_test) {
    ////////// LimitedStack MoveToValtypes check //////////
    {
        LimitedStack limitedStack(100);
        std::vector<valtype> valtypeVector;

        valtype vtype0({0xab, 0xcd});
        valtype vtype1({0xef, 0x12});

        limitedStack.push_back(vtype0);
        limitedStack.push_back(vtype1);

        limitedStack.MoveToValtypes(valtypeVector);

        BOOST_CHECK_EQUAL(valtypeVector[0][0], vtype0.at(0));
        BOOST_CHECK_EQUAL(valtypeVector[0][1], vtype0.at(1));
        BOOST_CHECK_EQUAL(valtypeVector[1][0], vtype1.at(0));
        BOOST_CHECK_EQUAL(valtypeVector[1][1], vtype1.at(1));
        BOOST_CHECK_EQUAL(limitedStack.size(), 0U);
    }
}

BOOST_AUTO_TEST_CASE(limitedvector_append_test) {
    ////////// LimitedVector append check //////////
    {
        valtype vtype;
        LimitedStack limitedStack(100);
        vtype.push_back({0xab});
        vtype.push_back({0xcd});

        limitedStack.push_back({0xef});
        limitedStack.push_back(vtype);

        LimitedVector &limitedVector1 = limitedStack.stacktop(-2);
        size_t size_before = limitedVector1.size();
        LimitedVector limitedVector2 = limitedStack.stacktop(-1);

        limitedVector1.append(limitedVector2);

        BOOST_CHECK_EQUAL(limitedVector1.size(), size_before + 2);
        BOOST_CHECK_EQUAL(limitedVector1.GetElement().at(0), 0xef);
        BOOST_CHECK_EQUAL(limitedVector1.GetElement().at(1), 0xab);
        BOOST_CHECK_EQUAL(limitedVector1.GetElement().at(2), 0xcd);
    }
}

BOOST_AUTO_TEST_CASE(limitedvector_padright_test) {
    ////////// LimitedVector padRight check //////////
    {
        // Pad > size
        {
            valtype vtype;
            LimitedStack limitedStack(100);
            unsigned pad_size = 10;
            limitedStack.push_back({});
            LimitedVector &limitedVector = limitedStack.stacktop(-1);
            size_t size_before = limitedVector.size();

            BOOST_CHECK_LT(size_before, pad_size);
            limitedVector.padRight(pad_size, 0);
            BOOST_CHECK_EQUAL(limitedVector.size(), pad_size);
        }

        // Pad < size
        {
            LimitedStack limitedStack(100);
            unsigned pad_size = 10;
            valtype vtype(pad_size + 2, 0xcd);

            limitedStack.push_back(vtype);
            LimitedVector limitedVector = limitedStack.stacktop(-1);

            limitedVector.padRight(pad_size, 0);
            BOOST_CHECK_EQUAL(limitedVector.size(), pad_size + 2);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
