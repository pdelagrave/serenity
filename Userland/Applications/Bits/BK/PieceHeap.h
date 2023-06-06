/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Applications/Bits/Peer.h"
#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/RefCounted.h>
#include <AK/Types.h>
#include <AK/NonnullRefPtr.h>

namespace Bits {

class Peer;

struct PieceStatus : public RefCounted<PieceStatus> {
    PieceStatus(u64 index_in_torrent)
        : index_in_torrent(index_in_torrent)
    {
    }
    Optional<size_t> index_in_heap = {};
    u64 index_in_torrent;
    size_t key() const { return havers.size(); }
    HashMap<NonnullRefPtr<Peer>, nullptr_t> havers;
    bool currently_downloading { false };
};

namespace BK {

// Based on AK::BinaryHeap
// Poor man's version of an intrusive binary heap/priority queue
class PieceHeap {
    using Value = NonnullRefPtr<PieceStatus>;
public:
    PieceHeap() = default;
    ~PieceHeap() = default;

    [[nodiscard]] size_t size() const { return m_size; }
    [[nodiscard]] bool is_empty() const { return m_size == 0; }

    void insert(Value value)
    {
        VERIFY(m_size < m_capacity);
        auto index = m_size++;
        m_elements.empend(value);
        value->index_in_heap = index;
        heapify_up(index);
    }

    Value pop_min()
    {
        VERIFY(!is_empty());
        auto index = --m_size;
        swap(m_elements[0], m_elements[index]);
        heapify_down(0);
        m_elements[index]->index_in_heap.clear();
        return m_elements[index];
    }

    [[nodiscard]] Value peek_min() const
    {
        VERIFY(!is_empty());
        return m_elements[0];
    }

    [[nodiscard]] size_t peek_min_key() const
    {
        VERIFY(!is_empty());
        return m_elements[0]->key();
    }

    void clear()
    {
        m_size = 0;
    }

    void update(Value value)
    {
        auto index = value->index_in_heap.value();
        auto& element = m_elements[index];
        VERIFY(value == element);

        auto parent = (index - 1) / 2;
        if (index > 0 && value->key() < m_elements[parent]->key())
            heapify_up(index);
        else
            heapify_down(index);
    }

private:
    static constexpr u64 m_capacity = 100000;

    void swap(Value& a, Value& b) {
        AK::swap(a, b);
        AK::swap(a->index_in_heap, b->index_in_heap);
    }

    void heapify_down(size_t index)
    {
        while (index * 2 + 1 < m_size) {
            auto left_child = index * 2 + 1;
            auto right_child = index * 2 + 2;

            auto min_child = left_child;
            if (right_child < m_size && m_elements[right_child]->key() < m_elements[min_child]->key())
                min_child = right_child;

            if (m_elements[index]->key() <= m_elements[min_child]->key())
                break;
            swap(m_elements[index], m_elements[min_child]);
            index = min_child;
        }
    }

    void heapify_up(size_t index)
    {
        while (index != 0) {
            auto parent = (index - 1) / 2;

            if (m_elements[index]->key() >= m_elements[parent]->key())
                break;
            swap(m_elements[index], m_elements[parent]);
            index = parent;
        }
    }

    Vector<Value, m_capacity> m_elements;
    size_t m_size { 0 };
};

}
}
