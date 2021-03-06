//This file is part of EasyNetwork which is released under Apache License 2.0.
#ifndef EASYNETWORK_SERVER_COMPILED
#define EASYNETWORK_SERVER_COMPILED

#pragma once
#include <set>
#include "Connection.h"

namespace EasyNetwork
{
	template<typename BufferType, template<typename> class QueueType>
	class Server
	{
		using TemplatedConnection = Connection<BufferType, QueueType>;
		using TemplatedServer = Server<BufferType, QueueType>;
	public:
		Server(float ping_frequency = 60.0f, float ping_timeout = 5.0f, std::size_t queue_capacity = 1024);
		virtual ~Server();
		Server(const Server&) = delete;
		Server& operator=(const Server&) = delete;

		bool is_started() const;
		void start(int port);
		void stop();
		//broadcast buffer from one connection to other connected clients
		asio::io_context::count_type poll_events();
		void broadcast(const TemplatedConnection* const from, const BufferType& buffer);

		//callback to handle connection of new client
		std::function<void(TemplatedServer* const, TemplatedConnection* const)> connect_handler = nullptr;
		//callback to fill ping massage
		std::function<void(BufferType&)> ping_handler = nullptr;
		//callback to handle received buffers
		std::function<void(TemplatedServer* const, TemplatedConnection* const, const BufferType&)> receive_handler = nullptr;
		//callback to handle client disconnection.
		std::function<void(TemplatedServer* const, TemplatedConnection* const)> disconnect_handler = nullptr;
		//callback to handle error occured while server works
		ErrorHandler error_handler = nullptr;

	protected:
		asio::io_context	io_ctx;
		tcp::endpoint		endpoint;
		std::atomic<bool>	running = false;
		void				server_main();
		std::unique_ptr<tcp::acceptor> server;
		std::unique_ptr<std::thread> server_thread;//thread to wait while client connected

		std::set<TemplatedConnection*>	connections;
		std::mutex connections_mutex;
		asio::thread_pool				pool;//thread pool to handle client

		std::size_t queue_capacity;//for queues in connections
		float		ping_frequency;
		float		ping_timeout;
	};	

	template<typename BufferType, template<typename> class QueueType>
	inline Server<BufferType, QueueType>::Server(float ping_frequency, float ping_timeout, std::size_t queue_capacity) :
		pool(std::thread::hardware_concurrency() - 1)
	{
		this->queue_capacity = queue_capacity;
		this->ping_frequency = ping_frequency;
		this->ping_timeout = ping_timeout;
	}

	template<typename BufferType, template<typename> class QueueType>
	inline Server<BufferType, QueueType>::~Server()
	{
		stop();
	}

	template<typename BufferType, template<typename> class QueueType>
	inline bool Server<BufferType, QueueType>::is_started() const
	{
		return running;
	}

	template<typename BufferType, template<typename> class QueueType>
	inline void Server<BufferType, QueueType>::start(int port)
	{
		if (!running)
		{
			endpoint = tcp::endpoint(tcp::v4(), port);
			server_thread = std::make_unique<std::thread>(&Server::server_main, this);
		}
	}

	template<typename BufferType, template<typename> class QueueType>
	inline asio::io_context::count_type Server<BufferType, QueueType>::poll_events()
	{
		auto res = io_ctx.poll();
		return res;
	}

	template<typename BufferType, template<typename> class QueueType>
	inline void Server<BufferType, QueueType>::stop()
	{
		if (running) 
		{
			if (error_handler)
				error_handler("Server::Stop", 0, "Server is stoping");
			running = false;
			connections_mutex.lock();
			for (TemplatedConnection* conn : connections)
				conn->close();
			connections_mutex.unlock();
			pool.join();
			server.reset();
			server_thread->join();
			server_thread.reset();
			if (error_handler)
				error_handler("Server::Stop", 0, "Server stopped");
		}
	}

	template<typename BufferType, template<typename> class QueueType>
	inline void Server<BufferType, QueueType>::broadcast(const TemplatedConnection* const from, const BufferType& buffer)
	{
		std::size_t sent_count = 0;
		std::lock_guard<std::mutex> lk(connections_mutex);
		for (TemplatedConnection* conn : connections) {
			if (from != conn) {
				auto& new_buffer = conn->enqueue_buffer();
				std::memcpy(&new_buffer, &buffer, sizeof(BufferType));
				conn->send_buffer_async();
			}
		}
	}

	template<typename BufferType, template<typename> class QueueType>
	inline void Server<BufferType, QueueType>::server_main()
	{
		server = std::make_unique<tcp::acceptor>(io_ctx, endpoint);
		//enable NO_DELAY mode
		server->set_option(tcp::no_delay(true));
		running = true;
		while (is_started() && running)
		{
			TemplatedConnection* conn = new TemplatedConnection(io_ctx, queue_capacity);
			conn->error_handler = error_handler;
			conn->receive_handler = std::bind(receive_handler, this, conn,
																		std::placeholders::_1);
			if (!TemplatedConnection::accept(server.get(), *conn))
			{
				delete conn;
				continue;
			}
			std::lock_guard<std::mutex> lk(connections_mutex);
			connections.insert(conn);
			asio::post(pool, [this, conn]()
				{			
					if (connect_handler) connect_handler(this, conn);
					auto start = std::chrono::system_clock::now();
					std::chrono::duration<float> elapsed;
					while (is_started() && conn->is_connected())
					{
						auto end = std::chrono::system_clock::now();
						elapsed = end - start;
						if (elapsed.count() >= ping_frequency)
						{
							auto& ping_buffer = conn->enqueue_buffer();
							if (ping_handler) ping_handler(ping_buffer);
							if (conn->send_buffer(ping_timeout))
							{
								start = end;
							}
							else break;
						}
						conn->poll_events();
						poll_events();
					}
					//disconnect
					if (disconnect_handler && running)
						disconnect_handler(this, conn);
					std::lock_guard<std::mutex> lk(connections_mutex);
					connections.erase(conn);
					delete conn;
					if (error_handler)
						error_handler("Server::Server_main", 0, "connection handler exited");
				});
		}
		if (error_handler)
			error_handler("Server::Server_main", 0, "Server main exited");
	}
};

#endif