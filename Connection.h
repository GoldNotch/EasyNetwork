//This file is part of EasyNetwork which is released under Apache License 2.0.
#ifndef EASYNETWORK_CONNECTION_COMPILED
#define EASYNETWORK_CONNECTION_COMPILED

#pragma once
#include <asio.hpp>
using asio::ip::tcp;
#include <string>
#include <cstdlib>
#include <functional>

namespace EasyNetwork
{
	using ErrorHandler = std::function<void(const char* location, int code, const char* message)>;
	template<typename BufferType>
	using ReceiveHandler = std::function<void(const BufferType& buffer)>;

	//Interface of queue used in Connection for send/receive
	template<typename BufferType>
	class IBufferQueue
	{
	public:
		IBufferQueue(std::size_t initial_capacity) {}
		IBufferQueue(const IBufferQueue&) = delete;
		IBufferQueue& operator=(const IBufferQueue&) = delete;
		virtual ~IBufferQueue() {};

		virtual BufferType& alloc() = 0;
		virtual void push(const BufferType& data) = 0;
		virtual const BufferType& peek() = 0;
		virtual BufferType& poke() = 0;
		virtual void pop() = 0;
		virtual std::size_t size() const = 0;
	};

	template<typename BufferType, template<typename> class QueueType>
	class Server;//forward-declaration to make it friend to connection

	template<typename BufferType, template<typename> class QueueType>
	class Client;//forward-declaration to make it friend to connection

	//Connection responds for sending and receive.
	template<typename BufferType, template<typename> class QueueType>
	class Connection
	{
		friend class Server<BufferType, QueueType>;
		friend class Client<BufferType, QueueType>;

	public:
		Connection(asio::io_context& io_context, std::size_t queue_capacity = 1024);
		virtual ~Connection();
		Connection(const Connection&) = delete;
		Connection& operator=(const Connection&) = delete;

		//connect to remote host. Return true if connection is opened
		bool		connect(const std::string& remote_ip, int remote_port);
		//disconnect
		void		close();
		bool		is_connected() const;
		//When you want to send buffer, you must enqueue buffer first, then initialize it and then send
		BufferType& enqueue_buffer();
		//command to send buffer asyncronously
		void		send_buffer_async();
		//command yo send buffer syncronously. return true if it was sent. False returns when time is out or error occured
		bool		send_buffer(float timeout);

		//---- All handles must be set before start connection works ---------
		//callback to handle received buffer
		ReceiveHandler<BufferType> receive_handler = nullptr;
		//callback to handle errors occured while connection works
		ErrorHandler error_handler = nullptr;

		//accept connection on server side. Return true if acception is successful
		static bool accept(tcp::acceptor& acceptor, Connection& connection);

	protected:
		//---------- asio -------------
		//poll send/recv events and if receive_queue has something handle it
		void				poll_events();
		asio::io_context&	io_ctx;
		tcp::socket			socket;
		std::atomic<bool>	_is_connected = false;
		//events synchronization
		asio::strand<asio::io_context::executor_type> _strand = asio::make_strand(io_ctx);

		//-----------Queues--------------
		void on_buffer_sent(const asio::error_code& ec, std::size_t sent_size);
		QueueType<BufferType>	sent_queue;
		std::atomic<bool>		is_sending = false;
		std::size_t				buffers_to_send = 0;
		std::mutex				send_mutex;

		void on_buffer_received(const asio::error_code& ec, std::size_t recv_size);
		QueueType<BufferType>	recv_queue;
		std::atomic<bool>		is_receiving = false;
		std::size_t				received_buffers_count = 0;
		std::mutex				poll_mutex;

		//---------- Wait for send (syncronous send) -------
		std::condition_variable	wait_for_sent_cv;
		std::mutex				wait_for_sent_mx;
		bool					is_sent = false;
	};

	template<typename BufferType, template<typename> class QueueType>
	inline Connection<BufferType, QueueType>::Connection(asio::io_context& io_context,
												std::size_t queue_capacity)
		: io_ctx(io_context), socket(io_context),
		sent_queue(queue_capacity),
		recv_queue(queue_capacity)
	{
	}

	template<typename BufferType, template<typename> class QueueType>
	inline Connection<BufferType, QueueType>::~Connection()
	{
		close();
	}

	template<typename BufferType, template<typename> class QueueType>
	inline bool Connection<BufferType, QueueType>::accept(tcp::acceptor& acceptor, Connection& connection)
	{
		asio::error_code ec;
		acceptor.accept(connection.socket, ec);
		if (ec.value() && connection.error_handler)
			connection.error_handler("Connection::accept", ec.value(), ec.message().c_str());
		connection._is_connected = ec.value() == 0;
		//enable NO_DELAY mode
		connection.socket.set_option(tcp::no_delay(true));
		return connection._is_connected;
	}

	template<typename BufferType, template<typename> class QueueType>
	inline bool Connection<BufferType, QueueType>::connect(const std::string& remote_ip, int remote_port)
	{
		asio::error_code ec;
		_is_connected = false;
		auto addr = asio::ip::address::from_string(remote_ip, ec);
		if (ec.value() && error_handler) 
		{
			error_handler("Connection::connect", ec.value(), ec.message().c_str());
			return false;
		}
		tcp::endpoint endpoint = tcp::endpoint(addr, remote_port);
		socket.connect(endpoint, ec);
		if (ec.value() && error_handler) {
			error_handler("Connection::connect", ec.value(), ec.message().c_str());
			return false;
		}
		//enable NO_DELAY mode
		socket.set_option(tcp::no_delay(true));
		_is_connected = true;
		return true;
	}

	template<typename BufferType, template<typename> class QueueType>
	inline void Connection<BufferType, QueueType>::close()
	{
		poll_mutex.lock();
		_is_connected = false;
		socket.close();
		if (socket.is_open())
			socket.shutdown(socket.shutdown_both);
		poll_mutex.unlock();
	}

	template<typename BufferType, template<typename> class QueueType>
	inline bool Connection<BufferType, QueueType>::is_connected() const
	{
		return _is_connected && socket.is_open();
	}

	template<typename BufferType, template<typename> class QueueType>
	inline void Connection<BufferType, QueueType>::poll_events()
	{
		poll_mutex.lock();
		//if can receive something - receive
		if (!is_receiving && is_connected() && socket.available() > 0)
		{
			is_receiving = true;
			void* buffer = &recv_queue.alloc();
			auto handler = std::bind(&Connection::on_buffer_received, this, std::placeholders::_1, std::placeholders::_2);
			asio::async_read(socket, asio::buffer(buffer, sizeof(BufferType)),
				asio::bind_executor(_strand, handler));
		}
		//handle received buffers
		if (receive_handler && received_buffers_count > 0)
		{
			receive_handler(recv_queue.peek());
			recv_queue.pop();
			--received_buffers_count;
		}
		poll_mutex.unlock();
	}

	template<typename BufferType, template<typename> class QueueType>
	inline BufferType& Connection<BufferType, QueueType>::enqueue_buffer()
	{
		return sent_queue.alloc();
	}

	template<typename BufferType, template<typename> class QueueType>
	inline void Connection<BufferType, QueueType>::send_buffer_async()
	{
		send_mutex.lock();
		//if it is sending yet when you can't call another send command
		if (!is_sending && is_connected())
		{
			is_sending = true;
			auto handler = std::bind(&Connection::on_buffer_sent, this, std::placeholders::_1, std::placeholders::_2);
			asio::async_write(socket, asio::buffer(&sent_queue.peek(), sizeof(BufferType)),
				asio::bind_executor(_strand, handler));
		}
		++buffers_to_send;
		send_mutex.unlock();
		if (buffers_to_send > sent_queue.size())
			error_handler("Connection::send_buffer_async", -2, "Buffer wasn't allocated for sending");
	}

	template<typename BufferType, template<typename> class QueueType>
	inline bool Connection<BufferType, QueueType>::send_buffer(float timeout)
	{
		send_buffer_async();
		//wait for send
		is_sent = false;
		std::unique_lock<std::mutex> lk(wait_for_sent_mx);
		wait_for_sent_cv.wait_for(lk, std::chrono::duration<float>(timeout), [this] {return is_sent; });
		lk.unlock();
		return is_sent;
	}

	template<typename BufferType, template<typename> class QueueType>
	inline void Connection<BufferType, QueueType>::on_buffer_sent(const asio::error_code& ec, std::size_t sent_size)
	{
		send_mutex.lock();
		buffers_to_send--;
		sent_queue.pop();
		send_mutex.unlock();

		bool error_occured = (ec.value() || sent_size < sizeof(BufferType));
		if (error_occured && error_handler)
			error_handler("Connection::on_buffer_sent", ec.value(), ec.message().c_str());

		if (buffers_to_send > sent_queue.size()) 
		{
			error_handler("Connection::on_buffer_sent", -1, "race_condition::buffers_to_send > sent_queue.size()");
			error_occured = true;
		}

		//unlock waiting for send
		{
			std::lock_guard<std::mutex> lk(wait_for_sent_mx);
			is_sent = !error_occured;
		}
		wait_for_sent_cv.notify_one();

		if (is_connected() && buffers_to_send > 0 && !error_occured) {
			auto handler = std::bind(&Connection::on_buffer_sent, this, std::placeholders::_1, std::placeholders::_2);
			asio::async_write(socket, asio::buffer(&sent_queue.peek(), sizeof(BufferType)),
				asio::bind_executor(_strand, handler));
		}
		else
			is_sending = false;
	}

	template<typename BufferType, template<typename> class QueueType>
	inline void Connection<BufferType, QueueType>::on_buffer_received(const asio::error_code& ec, std::size_t recv_size)
	{
		poll_mutex.lock();
		//if receive_handler == nullptr we don't save received buffers because it leads to memory overhead
		if (receive_handler == nullptr)
			recv_queue.pop();
		else
			++received_buffers_count;
		poll_mutex.unlock();

		if (ec.value() && error_handler)
			error_handler("Connection::on_buffer_received", ec.value(), ec.message().c_str());

		if (received_buffers_count > recv_queue.size())
			error_handler("Connection::on_buffer_received", -1, "race condition::received_buffers_count >= recv_queue.size()");
		
		if (is_connected() && socket.available() > 0 && ec.value() != 0)
		{
			void* buffer = &(recv_queue.alloc());
			auto handler = std::bind(&Connection::on_buffer_received, this, std::placeholders::_1, std::placeholders::_2);
			asio::async_read(socket, asio::buffer((void*)buffer, sizeof(BufferType)),
				asio::bind_executor(_strand, handler));
		}
		else is_receiving = false;
	}
};

#endif