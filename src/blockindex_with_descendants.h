// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#pragma once

#include "chain.h"
#include "prevector.h"




/**
 * Helper class that provides a tree of block descendants.
 */
class BlockIndexWithDescendants
{
public:
    /**
     * One item in a tree.
     *
     * Provides pointer to block index, array of all its children and
     * allows moving to the next item in a tree.
     */
    class Item
    {
    public:
        /**
         * Constructor creates an item that has no parent and no children.
         */
        Item(CBlockIndex* blockIndex)
        : blockIndex(blockIndex)
        , parent(nullptr)
        , children()
        {}

        /**
         * Set parent of this item and update children array in the parent.
         *
         * This effectively places object at correct position in a tree and must be called
         * for all items whose parent is in a tree (i.e. all except root).
         *
         * @note This is the second part of object construction (hence the name that starts with the name
         *       of the class) and does things which could not be done by constructor because the parent
         *       of the object under construction may not yet exist.
         *       It must be called at most once on each object.
         */
        void Item_SetParent(Item* p)
        {
            assert(parent == nullptr && p!=this);
            parent = p;
            p->children.push_back(this);
        }

        CBlockIndex* BlockIndex() const
        {
            return blockIndex;
        }

        /**
         * Return parent of this item or nullptr if this item has no parent in a tree.
         */
        const Item* Parent() const
        {
            return parent;
        }

        /**
         * Return children of this item.
         *
         * @note The order of children in returned array is unspecified but it is persistent
         *       (i.e. order will not change between calls).
         */
        const prevector<1, const Item*>& Children() const
        {
            return children;
        }

        /**
         * Return pointer to the next Item in a tree or nullptr if this is the last.
         *
         * To traverse the whole tree of descendants, start from Root() and repeatedly call
         * this method until nullptr is returned. The order of traversal is such that parent
         * items are guaranteed to be before their children.
         *
         * Method implements a non-recursive depth first traversal of the tree.
         * Items are returned in the following order:
         *      1
         *     / \
         *    2   8
         *   /|\
         *  3 4 6
         *    | |
         *    5 7
         *
         * @note Traversal depends on the order of children, which is unspecified. This implies that
         *       exact traversal order is also unspecified. E.g.: If the order of children of block 1
         *       is reversed, above tree would be traversed in the following order: 1,8,2,3,4,5,6,7
         */
        const Item* Next() const
        {
            if(!children.empty())
            {
                // If item has children, then the next item is its first child.
                return children.front();
            }

            // If item has no children, the next item is its next sibling. If there is none, we repeatedly
            // go up one level and try again until we find one or we get to root of the tree.
            const Item* item = this;
            const Item* p = item->parent;
            while(p!=nullptr)
            {
                // NOTE: Here p is always the parent of item.

                // Find the next child of p after item.
                // NOTE: This search is the price we pay for using a non-recursive algorithm.
                //       But since number of children of a block is small (most often just one),
                //       the performance overhead should be negligible.
                auto it_nextChild = std::find(p->children.begin(), p->children.end(), item);
                assert(it_nextChild != p->children.end()); // item must always be found because p is its parent
                ++it_nextChild;
                if(it_nextChild != p->children.end())
                {
                    return *it_nextChild;
                }

                // This was the last child.
                // Continue by searching for the next sibling of p.
                item = p;
                p = p->parent;
            }

            // If there is no parent, we're done.
            return nullptr;
        }

    private:
        CBlockIndex* blockIndex;
        const Item* parent;
        prevector<1, const Item*> children; // prevector is used to reduce number of small heap allocations since most blocks have only one child
    };

    /**
     * Construct object containing a tree of all descendants for given block.
     *
     * @param blockIndex Root block in a tree.
     * @param mapBlockIndex Container with pointers to CBlockIndex objects.
     *                      All CBlockIndex objects in this container must have member nHeight set to correct value.
     *                      In addition, all objects higher than blockIndex must have pprev properly set to either
     *                      nullptr or its parent, which must also be present in container.
     *                      Can be any container that provides forward iteration and stores non-const pointer to CBlockIndex
     *                      in value_type::second.
     * @param maxHeight Descendants whose height is larger than this are not added to tree. This can be
     *                  used to avoid searching for and storing descendants that are not needed.
     */
    // NOTE: Implementation iterates over all elements in array mapBlockIndex to find descendants.
    //       This is not scalable, but is probably fine as long as number of blocks is not really big.
    //       This class is only used for updating the soft rejection status of blocks, which are normally
    //       near the chain tip (small number of descendants) and we also don't expect this to be done very often.
    template<class TMapBlockIndex>
    BlockIndexWithDescendants(CBlockIndex* blockIndex, const TMapBlockIndex& mapBlockIndex, std::int32_t maxHeight)
    : blocks( {blockIndex} ) // Item for block for which we need descendants is the root of the tree
    {
        // Find and store all blocks with larger height than given block up to maxHeight.
        // These blocks could be descendants or they could be on a different chain.
        mapBlockIndex.ForEach(
        [&](const CBlockIndex& index)
        {
            if(index.GetHeight() > blockIndex->GetHeight() &&
               index.GetHeight() <= maxHeight)
            {
                blocks.emplace_back(const_cast<CBlockIndex*>(&index));
            }
        });

        // Create temporary associative array to efficiently find an item in blocks array.
        std::unordered_map<CBlockIndex*, Item*> bi2item;
        bi2item.reserve(blocks.size());
        for(auto& item: blocks)
        {
            bi2item.emplace(item.BlockIndex(), &item);
        }

        // Variable 'blocks' now contains all descendants of given block and possibly
        // some other blocks that are on different chains and which we can ignore.
        // Now we can place each item to proper location in a tree by updating its children
        // array and pointer to parent.
        for(auto& item: blocks)
        {
            auto it_parent = bi2item.find(item.BlockIndex()->GetPrev());
            if(it_parent==bi2item.end())
            {
                // Either this block is on a different chain than the one for which we're searching for descendants or
                // this is the block for which we're searching for descendants. Either way, there is no parent in blocks array.
                // Note that this also applies if pprev is nullptr.
                continue; // nothing to do since items by default have no parent
            }

            // Set parent of this item, which also adds this item to the children array of the parent
            // placing it at the correct location in tree.
            item.Item_SetParent(it_parent->second);
        }
    }

    /**
     * Return the Item for the block that was passed to constructor (i.e. the root of a tree).
     *
     * Note that the parent of this item is nullptr.
     */
    const Item* Root() const
    {
        // First item in blocks array is the root of the tree.
        return &blocks.front();
    }

private:
    std::vector<Item> blocks;
};
