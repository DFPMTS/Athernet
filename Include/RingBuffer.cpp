#include <boost/circular_buffer.hpp>

template <typename T>
class RingBuffer {
	RingBuffer(size_t capacity)
		: m_circular_buffer { capacity }
	{
	}

	boost::circular_buffer<T> m_circular_buffer;
};