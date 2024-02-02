#include "ProxyServer.h"

#include <boost/asio.hpp>
#include <iostream>
#include <vector>

#include "ProxyFinder.h"

using namespace std;
using boost::asio::ip::tcp;
namespace asio = boost::asio;

struct ProxyServer::Data {
    Server defaultProxy;
    Server localServer;
    asio::awaitable<void> startServer();
    asio::awaitable<void> serveClient(tcp::socket socket);
};

ProxyServer::ProxyServer(const Config& config)
    : d_ptr{make_unique<Data>()}
{
    d_ptr->defaultProxy = Server::fromString(config.server);
    d_ptr->localServer = Server::fromString(config.listen);
    if (d_ptr->localServer.empty()) {
        d_ptr->localServer.host = "127.0.0.1";
        d_ptr->localServer.port = "3128";
    }
}

ProxyServer::~ProxyServer()
{
}

void ProxyServer::run()
{
    asio::io_context context;
    asio::co_spawn(context, d_ptr->startServer(), asio::detached);
    context.run();
}

asio::awaitable<void> ProxyServer::Data::startServer()
{
    auto executor = co_await asio::this_coro::executor;
    tcp::resolver resolver{executor};
    tcp::acceptor acceptor{executor};
    try {
        auto result = resolver.resolve(localServer.host, localServer.port);
        acceptor = tcp::acceptor{executor, *result};
    } catch (...) {
        cerr << "Failed to start server on " << localServer.host << ':' << localServer.port << endl;
        co_return;
    }
    cout << "Server started on " << acceptor.local_endpoint() << endl;
    while (true) {
        tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);
        asio::co_spawn(executor, serveClient(move(socket)), asio::detached);
    }
}

asio::awaitable<void> ProxyServer::Data::serveClient(tcp::socket socket)
{
    vector<char> buf(1024);
    while (true) {
        auto bytesRead = co_await socket.async_read_some(asio::buffer(buf), asio::use_awaitable);
        co_await asio::async_write(socket, asio::buffer(buf, bytesRead), asio::use_awaitable);
    }
}
