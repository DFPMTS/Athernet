#include "RingBuffer.hpp"
#include <JuceHeader.h>

namespace AthernetUnitTest {

class RingBufferTest : public juce::UnitTest {
public:
	RingBufferTest()
		: juce::UnitTest("Ring Buffer")
	{
	}
	void runTest() override
	{
		beginTest("Ring Buffer Sanity");
		Athernet::RingBuffer<int> ring_buffer { 10 };
		for (int i = 1; i <= 10; ++i)
			ring_buffer.push(i);
		expectEquals(ring_buffer.dot_product(0, { 1, 2, 3 }), 14);

		ring_buffer.pop();

		expectEquals(ring_buffer.dot_product(0, { 1, 2, 3 }), 20);

		ring_buffer.pop();
		ring_buffer.pop();

		expectEquals(ring_buffer.dot_product(1, { 1, 2, 3 }), 38);

		expectThrows(ring_buffer.dot_product(8, { 1, 2, 3 }));
	}
};

static RingBufferTest ring_buffer_test;
}