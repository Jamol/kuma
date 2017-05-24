#ifndef _CC_QUEUE_H
#define _CC_QUEUE_H

#include <type_traits>
///////////////////////////////////////////////////////////////////////////////////////
// ATTENTION!!! CC_Queue only support one thread write/one thread read mode
template <class E>
class CC_Queue
{
public:
    CC_Queue ()
    : m_max_size(0)
    , m_read_pos(0)
    , m_write_pos(0)
    , m_enque_count(0)
    , m_deque_count(0)
    , m_elements(nullptr)
    {}
    ~CC_Queue()
    {
        if(m_elements) {
            delete[] m_elements;
            m_elements = nullptr;
        }
    }
    bool create(int32_t max_size)
    {
        if(max_size <= 0) {
            max_size = 1024;
        }
        m_max_size = max_size;
        m_elements = new E[max_size];
        //memset(m_elements, 0, max_size*sizeof(E));
        return true;
    }
    bool full()
    {
        if(m_max_size <= 0) {
            return true;
        }
        return ((m_write_pos + 1) % m_max_size == m_read_pos);
    }
    bool empty()
    {
        if(m_max_size <= 0) {
            return true;
        }
        return (m_write_pos == m_read_pos);
    }
    bool enqueue(const E& e)
    {
        if(full()) {
            return false;
        }
        m_elements[m_write_pos] = e;
        int32_t tmp_pos = m_write_pos +1;
        if(tmp_pos >= m_max_size) {
            tmp_pos = 0;
        }
        m_write_pos = tmp_pos;
        ++m_enque_count;
        return true;
    }
    bool enqueue(const E&& e)
    {
        if(full()) {
            return false;
        }
        m_elements[m_write_pos] = std::forward<E>(e);
        int32_t tmp_pos = m_write_pos +1;
        if(tmp_pos >= m_max_size) {
            tmp_pos = 0;
        }
        m_write_pos = tmp_pos;
        ++m_enque_count;
        return true;
    }
    bool front(E& e)
    {
        if(empty()) {
            return false;
        }
        e = m_elements[m_read_pos];
        return true;
    }
    bool dequeue(E& e)
    {
        if(empty()) {
            return false;
        }
        e = std::move(m_elements[m_read_pos]);
        //memset(&m_elements[m_read_pos], 0, sizeof(E));
        int32_t tmp_pos = m_read_pos +1;
        if(tmp_pos >= m_max_size) {
            tmp_pos = 0;
        }
        m_read_pos = tmp_pos;
        ++m_deque_count;
        return true;
    }
    uint32_t size()
    {// it is not precise
        return m_enque_count - m_deque_count;
    }
    
private:
    int32_t	m_max_size;
    int32_t	m_read_pos;
    int32_t	m_write_pos;
    uint32_t m_enque_count;
    uint32_t m_deque_count;
    E*    	m_elements;
};
///////////////////////////////////////////////////////////////////////////////////////

#endif
