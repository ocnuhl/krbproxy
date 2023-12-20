#include "ProxyServer.h"

#include <boost/asio.hpp>
#include <iostream>
#include <vector>

using namespace std;
using boost::asio::ip::tcp;
namespace asio = boost::asio;

struct ProxyServer::Data {
    string host;
    string port;
    asio::awaitable<void> startServer();
    asio::awaitable<void> serveClient(tcp::socket socket);
};

ProxyServer::ProxyServer(const Config& config)
    : d_ptr{make_unique<Data>()}
{
    if (!config.listen.empty()) {
        auto pos = config.listen.rfind(':');
        if (pos > 0 && pos < config.listen.size() - 1) {
            d_ptr->host = config.listen.substr(0, pos);
            d_ptr->port = config.listen.substr(pos + 1, string::npos);
        } else {
            cerr << "Invalid listen address: " << config.listen << endl;
        }
    }
    if (d_ptr->host.empty()) {
        d_ptr->host = "127.0.0.1";
        d_ptr->port = "3128";
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
        auto result = resolver.resolve(host, port);
        acceptor = tcp::acceptor{executor, *result};
    } catch (...) {
        cerr << "Failed to start server on " << host << ':' << port << endl;
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
