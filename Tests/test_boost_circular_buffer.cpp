//
//  test_boost_circular_buffer.cpp
//  NewProject - ConsoleApp
//
//  Created by Han Letong on 2023/10/1.
//

#include <JuceHeader.h>
#include <boost/circular_buffer.hpp>
#include <iostream>
#include <stdio.h>

class CircularBufferTest : public juce::UnitTest {
public:
    CircularBufferTest()
        : juce::UnitTest("Circular Buffer Sanity Test")
    {
    }
    void runTest() override
    {
        beginTest("Circular Buffer Sanity");
        auto circular_buffer = boost::circular_buffer<int>(10);
        for (int i = 0; i < 10; ++i)
            circular_buffer.push_back(i);
        for (int i = 0; i < 5; ++i)
            circular_buffer.push_back(i);

        expect(circular_buffer[0] == 5, "Circular Buffer Fail!");
        expect(circular_buffer[1] == 6, "Circular Buffer Fail!");
        expect(circular_buffer[2] == 7, "Circular Buffer Fail!");
        expect(circular_buffer[8] == 3, "Circular Buffer Fail!");
        expect(circular_buffer[9] == 4, "Circular Buffer Fail!");
        
    }
};

static CircularBufferTest circular_buffer_test;

