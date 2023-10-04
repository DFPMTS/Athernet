#pragma once

#include <boost/circular_buffer.hpp>
#include <vector>

namespace Athernet {

template <typename T>
class RingBuffer {
public:
	RingBuffer(size_t capacity)
		: m_circular_buffer { capacity }
	{
	}

	// ? Maybe refactor with perfect forwarding

	void push(const T& item)
	{
		m_circular_buffer.push_back(item);
	}

	void push(T&& item)
	{
		m_circular_buffer.push_back(std::move(item));
	}

	void push(const std::vector<T>& vec_item)
	{
		for (const auto& item : vec_item)
			push(item);
	}

	void push(std::vector<T>&& vec_item)
	{
		for (auto&& item : vec_item)
			push(std::move(item));
	}

	T& front()
	{
		return m_circular_buffer.front();
	}

	void pop()
	{
		m_circular_buffer.pop_front();
	}

	T& operator[](size_t index)
	{
		return m_circular_buffer[index];
	}

	size_t size() const
	{
		return m_circular_buffer.size();
	}

	size_t capacity() const
	{
		return m_circular_buffer.capacity();
	}

	/*!
		applies dot product on [index, index+vec.size())
		needs T * T -> T, T + T -> T
		\throw std::out_of_range if index+vec.size() > this->size()
	*/
	T dot_product(size_t index, std::vector<T> vec)
	{
		T result {};
		for (size_t i = 0; i < vec.size(); ++i)
			result = result + m_circular_buffer.at(index + i) * vec[i];
		return result;
	}

private:
	boost::circular_buffer<T> m_circular_buffer;
};

}