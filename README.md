# EasyNetwork
Easy hegh-level header-only real-time networking library. Communication based on TCP sockets from Asio library (without boost).

# How to integrate it
Download repo and paste it in your project. Link Asio Library to your project, then you can add Client.h, Connection.h and Server.h in your project, include them and use.

# How to use it
First you must declare POD type of buffer which will be sent and received. It can looks like:
```C++
struct Buffer
{
  int data1, data2;
  ...
  //no methods, constructors, etc
}
```
Then you must implement IBufferQueue interface. Queue manage memory and allocate buffers for sending and receiving. You can write your own queue (with manually memory management) or use written (like std::queue).
```C++
template<typename T>
class Queue : public IBufferQueue<T>
{
public:
  Queue(std::size_t initial_capacity) { ... }
  ~Queue()                            { ... }
  T& alloc() override                 { ... }
  void push(const T& data) override   { ... }
  const T& peek() override            { ... }
  T& poke() override                  { ... }
  void pop() override                 { ... }
  std::size_t size() const override   { ... }
private:
  //for example: std::queue<T> queue;
}
```
And then you can create client and server

## Server
First you declare Server variable.
```C++
EasyNetwork::Server<Buffer, Queue> server(<ping_frequency>, <ping_timeout>, <capacity for queues>);
```
- ping_frequency - time interval(in seconds) between ping;
- ping_timeout - how long to wait for ping answer (in seconds);
- capacity - initial capacity for queues to minimize memory allocation;

Then you should to set handlers. You can use lambdas for them. If you don't set them, your server will be without any useful logic
```C++
  server.error_handler = [](const char* location, int code, const char* message){...};
  server.ping_handler = [](Buffer& buffer) {...}; //used to init ping message (to not send unknown buffer)
  server.connect_handler = [this](_Server* const _server, _Connection* const connection){...};
  server.disconnect_handler = [this](_Server* const _server, _Connection* const connection) {...};
  server.receive_handler = [this](_Server* const _server, _Connection* const connection, const Buffer& buffer){...}
```

And then just start server. Server always generates events and you must poll them. So after start write infinite loop with polling events
```C++
  server.start(<port>);
  while(true) server.poll_events();
  server.stop();
```

## Client
Client creates in same way. First you declare client variable;
```C++
EasyNetwork::Client<Buffer, Queue> client(<capacity for queues>, <has_separate_thread_for_event_polling>);
```
- capacity for queues - initial capacity for queues to minimize memory allocation;
- has_separate_thread_for_event_polling - if true, there will be created another thread with infinite loop to call poll_events();

Then you set handlers. There is only two handlers: on receive and on error. You also can use lambdas.
```C++
  client.error_handler = [](const char* location, int code, const char* message){...};
  client.receive_handler = [](const Buffer& buffer){...};
  //if you want set member method then use std::bind. F.e. client.receive_handler = std::bind(&MyClient::OnReceive, this, std::placeholders::_1);
```

And finally you can connect to remote host.
```C++
  if (!client.connect("127.0.0.1", 8080))
		printf("Can't connect to remote host");
```
That's it, client connected. It will disconnect in destructor.
Now you can send something. Before send you must get new buffer, which you will fill your data. 
```C++
  Buffer& buffer = client.enqueue_buffer();
  //fill buffer
  ...
  client.send_buffer_async();//or send_buffer(<timout>);
```
Also see examples.
