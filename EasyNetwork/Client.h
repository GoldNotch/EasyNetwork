// Fill out your copyright notice in the Description page of Project Settings.
#ifndef EASYNETWORK_CLIENT_COMPILED
#define EASYNETWORK_CLIENT_COMPILED

#pragma once
#include <cstdlib>
#include "Connection.h"

namespace EasyNetwork {
	template<typename BufferType, template<typename> class QueueType>
	class Client
	{
	public:
		Client(std::size_t queue_initial_capacity = 1024, bool polling_in_thread = false);
		virtual ~Client();
		Client(const Client&) = delete;
		Client& operator=(const Client&) = delete;

		//-------------- API ----------------------
		bool		connect(const std::string& remote_ip, int remote_port);
		void		disconnect();
		void		poll_events();
		bool		is_connected() const;
		BufferType&	enqueue_buffer();
		void		send_buffer_async();
		bool		send_buffer(float timeout);

		ReceiveHandler<BufferType> receive_handler = nullptr;
		ErrorHandler error_handler = nullptr;

	protected:
		//----------- Asio ---------------
		asio::io_context		io_ctx;
		Connection<BufferType, QueueType>	connection;
		asio::thread*			io_thread = nullptr;
		bool					io_in_thread = false;

		//---------- wait for send --------------
		std::condition_variable wait_for_sent_cv;
		std::mutex				wait_for_sent_mx;
		bool					is_sent;
	};

	template<typename BufferType, template<typename> class QueueType>
	inline Client<BufferType, QueueType>::Client(std::size_t queue_initial_capacity, bool polling_in_thread) :
		connection(io_ctx, queue_initial_capacity)
	{
		this->io_in_thread = polling_in_thread;
	}
	
	template<typename BufferType, template<typename> class QueueType>
	inline Client<BufferType, QueueType>::~Client()
	{
		disconnect();
	}

	template<typename BufferType, template<typename> class QueueType>
	inline bool Client<BufferType, QueueType>::connect(const std::string& remote_ip, int remote_port)
	{
		connection.error_handler = error_handler;
		connection.receive_handler = receive_handler;
		bool success = connection.connect(remote_ip, remote_port);
		if (success && io_in_thread)
			io_thread = new asio::thread([this]() 
				{ 
					while (is_connected())
					{
						poll_events();
						std::this_thread::sleep_for(std::chrono::microseconds(2));
					}
				});
		return success;
	}

	template<typename BufferType, template<typename> class QueueType>
	inline void Client<BufferType, QueueType>::disconnect()
	{
		connection.close();
		if (io_thread) {
			io_thread->join();
			delete io_thread;
			io_thread = nullptr;
		}
	}

	template<typename BufferType, template<typename> class QueueType>
	inline bool Client<BufferType, QueueType>::is_connected() const
	{
		return connection.is_connected();
	}

	template<typename BufferType, template<typename> class QueueType>
	inline void Client<BufferType, QueueType>::poll_events()
	{
		connection.poll_events();
		//poll asio events
		io_ctx.poll();
		//if (io_ctx.stopped()) {
		//	io_ctx.restart();
		//}
	}

	template<typename BufferType, template<typename> class QueueType>
	inline BufferType& Client<BufferType, QueueType>::enqueue_buffer()
	{
		return connection.enqueue_buffer();
	}

	template<typename BufferType, template<typename> class QueueType>
	inline void Client<BufferType, QueueType>::send_buffer_async()
	{
		connection.send_buffer_async();
	}

	template<typename BufferType, template<typename> class QueueType>
	inline bool Client<BufferType, QueueType>::send_buffer(float timeout)
	{
		return connection.send_buffer(timeout);
	}

};
#endif