#include <cstdio>
#include "../Server.h"
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
	Queue(std::size_t capacity) : IBufferQueue<T>(capacity) {}
	~Queue() override {}
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
	Server<Buffer, Queue> server;
	server.connect_handler = [](Server<Buffer, Queue>* server, Connection<Buffer, Queue>* connection)
		{
			printf("Client connected\n");
		};
	server.disconnect_handler = [](Server<Buffer, Queue>* server, Connection<Buffer, Queue>* connection)
		{
			printf("client disconnected");
		};
	server.receive_handler = [](Server<Buffer, Queue>* server, Connection<Buffer, Queue>* connection,
			const Buffer& buffer)
	{
		printf("received message from %i: %i bytes\n", buffer.multiplayer_id, sizeof(Buffer));
		long long sum = 0;
		for (int i = 0; i < 1022; ++i)
			sum += buffer.data[i];
		printf("%lli\n", sum);//to check message is correct (not junk)
		server->broadcast(connection, buffer);
	};
	server.error_handler = [](const char* location, int code, const char* message)
	{
		printf("error in %s: %i - %s\n", location, code, message);
	};

	server.start(8080);
	while (true)
	{
		server.poll_events();
	}
	server.stop();
	return 0;
}