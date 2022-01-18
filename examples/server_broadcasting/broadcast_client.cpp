#include <cstdio>
#include <cstring>
#include "../Client.h"
#include <chrono>
#include <queue>
using namespace EasyNetwork;
using namespace std;

struct Buffer
{
	int multiplayer_id;
	int signature;
	int data[1022];
};

template<typename T>
class Queue : public IBufferQueue<T>
{
public:
	Queue(std::size_t capacity): IBufferQueue<T>(capacity) {}
	~Queue() override{}
	T& alloc() override 
	{
		queue.emplace();
		return queue.back();
	}
	void push(const T& data) override
	{
		queue.push(data);
	}
	const T& peek() override
	{
		return queue.front();
	}
	T& poke() override
	{
		return queue.front();
	}
	void pop() override
	{
		queue.pop();
	}
	std::size_t size() const override
	{
		return queue.size();
	}
protected:
	std::queue<T> queue;
};

int main()
{
	setlocale(LC_ALL, "Russian");
	srand(time(NULL));
	int m_id = rand() % 10 + 1;
	Client<Buffer, Queue> client(1024, true);
	client.error_handler = [](const char* location, int code, const char* message)
	{
		printf("error in %s: %i - %s\n", location, code, message);
	};
	client.receive_handler = [](const Buffer& buffer)
	{
		printf("received %i bytes from %i user ", sizeof(Buffer), buffer.multiplayer_id);
		if (buffer.signature == 2)
		{
			uint64_t sum = 0;
			for (int i = 0; i < 1022; ++i)
				sum += buffer.data[i];
			printf("sum: %i\n", sum);
		}
		else if (buffer.signature == 3)
		{
			printf("user %i disconnected\n", buffer.multiplayer_id);
		}
	};

	client.connect("192.168.0.50", 8080);
	auto& data = client.enqueue_buffer();
	data.multiplayer_id = m_id;
	data.signature = 1;
	if (client.send_buffer(10))
	{
		printf("session started\n");
	}
	else
		return -1;

	while(true)
	{
		auto& d = client.enqueue_buffer();
		d.multiplayer_id = m_id;
		d.signature = 2;
		for (int j = 0; j < 1022; ++j)
			d.data[j] = m_id;
		client.send_buffer_async();
		std::this_thread::sleep_for(std::chrono::microseconds(2));
	}

	system("pause");
	return 0;
}

//
// blocking_tcp_echo_client.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2021 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

//#include <cstdlib>
//#include <cstring>
//#include <iostream>
//#include <asio.hpp>
//
//using asio::ip::tcp;
//
//enum { max_length = 1024 };
//
//int main(int argc, char* argv[])
//{
//    setlocale(LC_ALL, "Russian");
//    try
//    {
//
//        asio::io_context io_context;
//
//        tcp::socket s(io_context);
//        tcp::resolver resolver(io_context);
//        asio::connect(s, resolver.resolve("192.168.0.103", "8080"));
//
//        std::cout << "Enter message: ";
//        char request[max_length];
//        std::cin.getline(request, max_length);
//        size_t request_length = std::strlen(request);
//        asio::async_write(s, asio::buffer(request, request_length), 
//            [](const asio::error_code& ec, std::size_t size) 
//            {
//            
//            });
//
//        char reply[max_length];
//        size_t reply_length = asio::read(s,
//            asio::buffer(reply, request_length));
//        std::cout << "Reply is: ";
//        std::cout.write(reply, reply_length);
//        std::cout << "\n";
//    }
//    catch (std::exception& e)
//    {
//        std::cerr << "Exception: " << e.what() << "\n";
//    }
//    system("pause");
//    return 0;
//}