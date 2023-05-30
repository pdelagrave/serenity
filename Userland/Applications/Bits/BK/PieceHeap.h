/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Types.h>

namespace Bits {
namespace BK {

// Simpler for now to just copy AK::BinaryHeap instead of inheriting it.
template<typename K, typename V, size_t Capacity>
class PieceHeap {
public:
    PieceHeap() = default;
    ~PieceHeap() = default;

    // This constructor allows for O(n) construction of the heap (instead of O(nlogn) for repeated insertions)
    PieceHeap(K keys[], V values[], size_t size)
    {
        VERIFY(size <= Capacity);
        m_size = size;
        for (size_t i = 0; i < size; i++) {
            m_elements[i].key = keys[i];
            m_elements[i].value = values[i];
        }

        for (ssize_t i = size / 2; i >= 0; i--) {
            heapify_down(i);
        }
    }

    [[nodiscard]] size_t size() const { return m_size; }
    [[nodiscard]] bool is_empty() const { return m_size == 0; }

    V at(size_t index)
    {
        VERIFY(index < m_size);
        return m_elements[index].value;
    }

    K at_key(size_t index)
    {
        VERIFY(index < m_size);
        return m_elements[index].key;
    }

    size_t insert(K key, V value)
    {
        VERIFY(m_size < Capacity);
        auto index = m_size++;
        m_elements[index].key = key;
        m_elements[index].value = value;
        return heapify_up(index);
    }

    V pop_min()
    {
        VERIFY(!is_empty());
        auto index = --m_size;
        swap(m_elements[0], m_elements[index]);
        heapify_down(0);
        return m_elements[index].value;
    }

    [[nodiscard]] V const& peek_min() const
    {
        VERIFY(!is_empty());
        return m_elements[0].value;
    }

    [[nodiscard]] K const& peek_min_key() const
    {
        VERIFY(!is_empty());
        return m_elements[0].key;
    }

    void clear()
    {
        m_size = 0;
    }

    size_t update_key(size_t index, u32 new_key_value)
    {
        VERIFY(index < m_size);
        auto& element = m_elements[index];
        auto old_key_value = element.key;
        element.key = new_key_value;
        if (new_key_value < old_key_value)
            return heapify_up(index);
        else
            return heapify_down(index);
    }

private:
    size_t heapify_down(size_t index)
    {
        while (index * 2 + 1 < m_size) {
            auto left_child = index * 2 + 1;
            auto right_child = index * 2 + 2;

            auto min_child = left_child;
            if (right_child < m_size && m_elements[right_child].key < m_elements[min_child].key)
                min_child = right_child;

            if (m_elements[index].key <= m_elements[min_child].key)
                break;
            swap(m_elements[index], m_elements[min_child]);
            index = min_child;
        }
        return index;
    }

    size_t heapify_up(size_t index)
    {
        while (index != 0) {
            auto parent = (index - 1) / 2;

            if (m_elements[index].key >= m_elements[parent].key)
                break;
            swap(m_elements[index], m_elements[parent]);
            index = parent;
        }
        return index;
    }

    struct {
        K key;
        V value;
    } m_elements[Capacity];
    size_t m_size { 0 };
};

}
}
