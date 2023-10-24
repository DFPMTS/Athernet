#include "Config.hpp"
#include "JuceHeader.h"
#include "PHY_Receiver.hpp"
#include "PHY_Sender.hpp"
#include "RingBuffer.hpp"
#include "schifra_error_processes.hpp"
#include "schifra_galois_field.hpp"
#include "schifra_galois_field_polynomial.hpp"
#include "schifra_reed_solomon_bitio.hpp"
#include "schifra_reed_solomon_block.hpp"
#include "schifra_reed_solomon_decoder.hpp"
#include "schifra_reed_solomon_encoder.hpp"
#include "schifra_sequential_root_generator_polynomial_creator.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#define PI acos(-1)

// #define WIN

#ifndef WIN
#define NOTEBOOK_DIR "/Users/dfpmts/Desktop/JUCE_Demos/NewProject/Extras/"s
#else
#define NOTEBOOK_DIR "D:/fa23/Athernet/Extras/"s
#endif

using namespace std::chrono_literals;
using namespace std::string_literals;

template <typename T> class PHY_layer : public juce::AudioIODeviceCallback {
public:
	PHY_layer()
		: config { Athernet::Config::get_instance() }
	{
	}

	virtual void audioDeviceAboutToStart([[maybe_unused]] juce::AudioIODevice* device) override { }

	virtual void audioDeviceIOCallbackWithContext([[maybe_unused]] const float* const* inputChannelData,
		[[maybe_unused]] int numInputChannels, [[maybe_unused]] float* const* outputChannelData,
		[[maybe_unused]] int numOutputChannels, [[maybe_unused]] int numSamples,
		[[maybe_unused]] const juce::AudioIODeviceCallbackContext& context) override
	{
		int samples_wrote = m_sender.pop_stream(outputChannelData[0], numSamples);

		for (int i = samples_wrote; i < numSamples; ++i) {
			outputChannelData[0][i] = 0;
		}

		m_receiver.push_stream(inputChannelData[0], numSamples);
	}

	virtual void audioDeviceStopped() override { }

	void send_frame(const std::vector<int>& frame) { m_sender.push_frame(frame); }

	std::vector<std::vector<int>> get_frames() { }

	~PHY_layer() { }

public:
	Athernet::PHY_Receiver<T> m_receiver;

private:
	Athernet::Config& config;

	Athernet::PHY_Sender<T> m_sender;
};

template <typename T> void random_test(PHY_layer<T>* physical_layer, int num_packets, int packet_length)
{
	auto sent_fd = fopen((NOTEBOOK_DIR + "sent.txt"s).c_str(), "wc");

	for (int i = 0; i < num_packets; ++i) {
		std::vector<int> a;
		for (int j = 0; j < packet_length; ++j) {
			if (rand() % 2)
				a.push_back(1);
			else
				a.push_back(0);
		}

		physical_layer->send_frame(a);
		for (const auto& x : a)
			fprintf(sent_fd, "%d", x);
		fprintf(sent_fd, "\n");
		fflush(sent_fd);
	}
	fclose(sent_fd);
}

namespace RS {
const int field_descriptor = 10;
const int generator_polynomial_index = 120;
const int generator_polynomial_root_count = 512;

/* Reed Solomon Code Parameters */
const int code_length = 1023;
const int fec_length = 512;
const int data_length = code_length - fec_length;

// const int field_descriptor = 4;
// const int generator_polynomial_index = 0;
// const int generator_polynomial_root_count = 7;

// /* Reed Solomon Code Parameters */
// const int code_length = 15;
// const int fec_length = 7;
// const int data_length = code_length - fec_length;

/* Instantiate Encoder and Decoder (Codec) */
typedef schifra::reed_solomon::encoder<code_length, fec_length, data_length> encoder_t;
typedef schifra::reed_solomon::decoder<code_length, fec_length, data_length> decoder_t;

std::vector<int> encode(std::vector<int> text)
{
	/* Instantiate Finite Field and Generator Polynomials */
	const schifra::galois::field field(field_descriptor, schifra::galois::primitive_polynomial_size08,
		schifra::galois::primitive_polynomial08);
	// const schifra::galois::field field(field_descriptor, schifra::galois::primitive_polynomial_size01,
	// 	schifra::galois::primitive_polynomial01);

	schifra::galois::field_polynomial generator_polynomial(field);

	if (!schifra::make_sequential_root_generator_polynomial(
			field, generator_polynomial_index, generator_polynomial_root_count, generator_polynomial)) {
		std::cout << "Error - Failed to create sequential root generator!" << std::endl;
		assert(0);
	}
	const encoder_t encoder(field, generator_polynomial);

	auto bit_code_length = field_descriptor * code_length;
	auto bit_data_length = field_descriptor * data_length;
	for (int i = (text.size() - 1) % (bit_data_length) + 1; i < bit_data_length; ++i) {
		text.push_back(0);
	}
	std::vector<int> encoded;
	for (int i = 0; i < text.size(); i += bit_data_length) {
		schifra::reed_solomon::block<code_length, fec_length> block;
		for (int j = 0; j < data_length; ++j) {
			block[j] = 0;
			for (int k = 0; k < field_descriptor; ++k) {
				block[j] |= (text[i + j * field_descriptor + k] << k);
			}
		}

		if (!encoder.encode(block)) {
			assert(0);
		}

		for (int i = 0; i < code_length; ++i) {
			for (int j = 0; j < field_descriptor; ++j) {
				if (block[i] & (1 << j)) {
					encoded.push_back(1);
				} else {
					encoded.push_back(0);
				}
			}
		}
	}
	return encoded;
}

std::vector<int> decode(std::vector<int> encoded)
{
	/* Instantiate Finite Field and Generator Polynomials */
	const schifra::galois::field field(field_descriptor, schifra::galois::primitive_polynomial_size08,
		schifra::galois::primitive_polynomial08);
	// const schifra::galois::field field(field_descriptor, schifra::galois::primitive_polynomial_size01,
	// 	schifra::galois::primitive_polynomial01);

	schifra::galois::field_polynomial generator_polynomial(field);

	if (!schifra::make_sequential_root_generator_polynomial(
			field, generator_polynomial_index, generator_polynomial_root_count, generator_polynomial)) {
		std::cout << "Error - Failed to create sequential root generator!" << std::endl;
		assert(0);
	}

	const decoder_t decoder(field, generator_polynomial_index);

	auto bit_code_length = field_descriptor * code_length;
	auto bit_data_length = field_descriptor * data_length;

	while (encoded.size() % bit_code_length) {
		encoded.pop_back();
	}

	std::vector<int> decoded;
	for (int i = 0; i < encoded.size(); i += bit_code_length) {

		schifra::reed_solomon::block<code_length, fec_length> block;
		for (int j = 0; j < code_length; ++j) {
			block[j] = 0;
			for (int k = 0; k < field_descriptor; ++k) {
				block[j] |= (encoded[i + j * field_descriptor + k] << k);
			}
		}

		if (!decoder.decode(block)) {
			assert(0);
		}

		for (int i = 0; i < data_length; ++i) {
			for (int j = 0; j < field_descriptor; ++j) {
				if (block[i] & (1 << j)) {
					decoded.push_back(1);
				} else {
					decoded.push_back(0);
				}
			}
		}
	}
	return decoded;
}
}

void* Project1_main_loop(void*)
{
	// Use RAII pattern to take care of initializing/shutting down JUCE
	juce::ScopedJuceInitialiser_GUI init;

	juce::AudioDeviceManager adm;
	adm.initialiseWithDefaultDevices(1, 1);

	auto device_setup = adm.getAudioDeviceSetup();
	device_setup.sampleRate = 48'000;
	device_setup.bufferSize = 256;

	auto physical_layer = std::make_unique<PHY_layer<float>>();

	auto device_type = adm.getCurrentDeviceTypeObject();

	{
		auto default_input = device_type->getDefaultDeviceIndex(true);
		auto input_devices = device_type->getDeviceNames(true);

		std::cerr << "-------Input-------\n";
		for (int i = 0; i < input_devices.size(); ++i)
			std::cerr << (i == default_input ? "x " : "  ") << input_devices[i] << "\n";
	}

	{
		auto default_output = device_type->getDefaultDeviceIndex(false);
		auto output_devices = device_type->getDeviceNames(false);

		std::cerr << "-------Output------\n";
		for (int i = 0; i < output_devices.size(); ++i)
			std::cerr << (i == default_output ? "x " : "  ") << output_devices[i] << "\n";
		std::cerr << "-------------------\n";
	}

	device_setup.inputDeviceName = "MacBook Pro Microphone";
	// device_setup.inputDeviceName = "USB Audio Device";
	// device_setup.outputDeviceName = "MacBook Pro Speakers";
	// device_setup.outputDeviceName = "USB Audio Device";

	adm.setAudioDeviceSetup(device_setup, false);

	std::cerr << "Please configure your ASIO:\n";

	std::cerr << "Running...\n";

	srand(static_cast<unsigned int>(time(0)));

	adm.addAudioCallback(physical_layer.get());

	// getchar();

	// random_test(physical_layer.get(), 100, 2 * (100 + 26));

	// // getchar();

	// std::this_thread::sleep_for(15s);

	// return 0;
	std::string file = "INPUT.txt";
	std::vector<int> text;
	int c;
	file = NOTEBOOK_DIR + file;
	auto fd = fopen(file.c_str(), "r");
	while ((c = fgetc(fd)) != EOF) {
		text.push_back(int(c - '0'));
	}
	if (fd)
		fclose(fd);

	auto ret = RS::encode(text);

	std::cerr << ret.size() << "\n";

	std::vector<int> a;
	int packet_len = 250;

	for (auto x : ret) {
		a.push_back(x);
		if (a.size() == packet_len) {
			physical_layer->send_frame(a);
			a.clear();
		}
	}

	if (a.size()) {
		for (int i = (int)a.size(); i < packet_len; ++i) {
			a.push_back(0);
		}
		physical_layer->send_frame(a);
	}

	std::this_thread::sleep_for(15s);

	std::vector<int> code;
	std::vector<int> frame;
	while (physical_layer->m_receiver.m_decoder_queue.pop(frame)) {
		for (auto x : frame)
			code.push_back(x);
	}

	auto res = RS::decode(code);

	for (int i = 0; i < text.size(); ++i) {
		if (res[i] != text[i]) {
			std::cerr << i << "\n";
		}
	}

	int file_id = 1;
	int file_len = 10000;
	std::string file_name = NOTEBOOK_DIR + std::to_string(file_id) + ".txt";
	remove(file_name.c_str());
	auto out_fd = fopen(file_name.c_str(), "wc");
	for (int i = 0; i < file_len; ++i) {
		fprintf(out_fd, "%d", res[i]);
	}
	fflush(out_fd);
	fclose(out_fd);

	// std::string s;
	// while (true) {
	// 	std::cin >> s;
	// 	static int group_flag = 0;
	// 	group_flag ^= 1;
	// 	if (s == "r") {
	// 		int num, len;
	// 		std::cin >> num >> len;
	// 		random_test(physical_layer.get(), num, len);
	// 	} else if (s == "s") {
	// 		std::string file;
	// 		std::vector<int> text;
	// 		int c;

	// 		std::cin >> file;
	// 		file = NOTEBOOK_DIR + file;
	// 		auto fd = fopen(file.c_str(), "r");
	// 		while ((c = fgetc(fd)) != EOF) {
	// 			text.push_back(int(c - '0'));
	// 		}
	// 		if (fd)
	// 			fclose(fd);

	// 		// int file_len = (int)text.size();
	// 		// std::vector<int> length;
	// 		// for (int i = 0; i < 16; ++i) {
	// 		// 	if (file_len & (1 << i)) {
	// 		// 		length.push_back(1);
	// 		// 	} else {
	// 		// 		length.push_back(0);
	// 		// 	}
	// 		// }

	// 		int num_packets = 100;
	// 		int packet_len = (int)(text.size() - 1) / num_packets + 1;

	// 		std::random_device seeder;
	// 		const auto seed = seeder.entropy() ? seeder() : time(nullptr);
	// 		std::mt19937 engine { static_cast<std::mt19937::result_type>(seed) };
	// 		std::uniform_int_distribution<int> window_start_distribution { 0, num_packets - 1 };
	// 		std::uniform_int_distribution<int> is_one_distribution { 1, 100 };

	// 		std::vector<std::vector<int>> mat;
	// 		std::vector<int> a;

	// 		// for (auto x : length) {
	// 		// 	a.push_back(x);
	// 		// 	if (a.size() == packet_len) {
	// 		// 		mat.push_back(std::move(a));
	// 		// 		a.clear();
	// 		// 	}
	// 		// }
	// 		for (auto x : text) {
	// 			a.push_back(x);
	// 			if (a.size() == packet_len) {
	// 				mat.push_back(std::move(a));
	// 				a.clear();
	// 			}
	// 		}

	// 		if (a.size()) {
	// 			for (int i = (int)a.size(); i < packet_len; ++i) {
	// 				a.push_back(0);
	// 			}
	// 			mat.push_back(std::move(a));
	// 		}

	// 		double packet_success_rate = 0.7;
	// 		int packets_to_send = static_cast<int>((num_packets + 25) / packet_success_rate);

	// 		auto sent_fd = fopen((NOTEBOOK_DIR + "sent.txt"s).c_str(), "wc");

	// 		std::vector<int> actual_frame;
	// 		int to_send = 0;
	// 		while (packets_to_send--) {
	// 			std::vector<int> start;
	// 			int start_point = window_start_distribution(engine);
	// 			for (int i = 0; i < 7; ++i) {
	// 				if (start_point & (1 << i)) {
	// 					start.push_back(1);
	// 				} else {
	// 					start.push_back(0);
	// 				}
	// 			}
	// 			std::vector<int> bit_map(19);
	// 			std::vector<int> frame = mat[start_point];
	// 			for (int i = 0; i < 19; ++i) {
	// 				bit_map[i] = (is_one_distribution(engine) < 60) ? 1 : 0;
	// 				if (bit_map[i]) {
	// 					int j = start_point + i + 1;
	// 					if (j >= 100)
	// 						j -= 100;
	// 					for (int k = 0; k < packet_len; ++k) {
	// 						frame[k] ^= mat[j][k];
	// 					}
	// 				}
	// 			}

	// 			for (auto x : start)
	// 				actual_frame.push_back(x);
	// 			for (auto x : bit_map)
	// 				actual_frame.push_back(x);
	// 			for (auto x : frame)
	// 				actual_frame.push_back(x);

	// 			if (to_send) {
	// 				// for (int i = 0; i < 126; ++i)
	// 				// 	fprintf(sent_fd, "%d", actual_frame[i]);
	// 				// fprintf(sent_fd, "\n");
	// 				// for (int i = 0; i < 126; ++i)
	// 				// 	fprintf(sent_fd, "%d", actual_frame[126 + i]);
	// 				for (int i = 0; i < actual_frame.size(); ++i)
	// 					fprintf(sent_fd, "%d", actual_frame[i]);

	// 				fprintf(sent_fd, "\n");
	// 				fflush(sent_fd);
	// 				physical_layer->send_frame(actual_frame);
	// 				actual_frame.clear();
	// 			}
	// 			to_send ^= 1;
	// 		}
	// 		fclose(sent_fd);
	// 	} else if (s == "e") {
	// 		break;
	// 	}
	// }

	adm.removeAudioCallback(physical_layer.get());

	return NULL;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
	// srand(time(0));
	// std::string file = "INPUT.txt";
	// file = NOTEBOOK_DIR + file;
	// auto fd = freopen(file.c_str(), "w", stdout);
	// for (int i = 0; i < 10000; ++i) {
	// 	if (rand() % 2) {
	// 		putchar('1');
	// 	} else {
	// 		putchar('0');
	// 	}
	// }
	// fclose(fd);
	// return 0;
	auto message_manager = juce::MessageManager::getInstance();
	message_manager->callFunctionOnMessageThread(Project1_main_loop, NULL);
	// message_manager->callFunctionOnMessageThread(Project1_Task2_loop, NULL);

	// should not be called manually -- juce::ScopedJuceInitialiser_GUI will do
	// juce::DeletedAtShutdown::deleteAll();

	juce::MessageManager::deleteInstance();
}