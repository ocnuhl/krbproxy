#include "ProxyServer.h"

#include <boost/asio.hpp>
#include <iostream>
#include <regex>

#include "ProxyAuth.h"
#include "ProxyFinder.h"

using namespace std;
using boost::asio::ip::tcp;
namespace asio = boost::asio;

struct ProxyServer::Data {
    Server defaultProxy;
    Server localServer;
    ProxyAuth proxyAuth;
    ProxyFinder proxyFinder;
    asio::awaitable<void> startServer();
    asio::awaitable<void> serveClient(tcp::socket client);
};

class ProxyServer::Session : public enable_shared_from_this<ProxyServer::Session>
{
public:
    Session(tcp::socket client, tcp::socket remote);
    void start();

private:
    void stop();
    asio::awaitable<void> reader();
    asio::awaitable<void> writer();
    tcp::socket client;
    tcp::socket remote;
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
    if (!config.pac.empty()) {
        d_ptr->proxyFinder.setPacFile(string{config.pac});
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
        tcp::socket client = co_await acceptor.async_accept(asio::use_awaitable);
        asio::co_spawn(executor, serveClient(move(client)), asio::detached);
    }
}

asio::awaitable<void> ProxyServer::Data::serveClient(tcp::socket client)
{
    string buf(1024, '\0');
    string_view bufview;
    string host, port;
    Server proxy = defaultProxy;
    if (proxy.empty()) {
        auto n = co_await client.async_read_some(asio::buffer(buf), asio::use_awaitable);
        bufview = string_view{buf.data(), n};
        auto pattern = "^\\w+ http://([^:/]+)(?::(\\d+))?/";
        if (buf.starts_with("CONNECT")) {
            pattern = "CONNECT ([^:]+):(\\d+) HTTP";
        }
        smatch match;
        regex_search(buf, match, regex{pattern});
        if (match.size() == 3) {
            host = match[1];
            port = match[2];
        }
        if (host.empty()) {
            cerr << "Bad request: " << buf << endl;
            co_return;
        }
        if (port.empty()) {
            port = "80";
        }
        proxy = proxyFinder.findProxyForHost(host);
    }

    tcp::resolver resolver{client.get_executor()};
    tcp::socket remote{client.get_executor()};
    if (proxy.empty()) {
        co_await asio::async_write(client, asio::buffer("HTTP/1.1 501 Not Implemented\r\n\r\n"sv), asio::use_awaitable);
        co_return;
    } else {
        auto result = co_await resolver.async_resolve(proxy.host, proxy.port, asio::use_awaitable);
        co_await remote.async_connect(*result, asio::use_awaitable);
        while (true) {
            if (bufview.empty()) {
                auto n = co_await client.async_read_some(asio::buffer(buf), asio::use_awaitable);
                bufview = string_view{buf.data(), n};
            }
            auto pos = bufview.find('\n');
            if (pos == bufview.npos) {
                --pos;
            }
            string_view line = bufview.substr(0, pos + 1);
            if (line == "\r\n") {
                break;
            }
            co_await asio::async_write(remote, asio::buffer(line), asio::use_awaitable);
            bufview.remove_prefix(line.size());
        }
        string_view authHeader = proxyAuth.getAuthHeader(proxy.host);
        if (!authHeader.empty()) {
            co_await asio::async_write(remote, asio::buffer(authHeader), asio::use_awaitable);
        }
        co_await asio::async_write(remote, asio::buffer(bufview), asio::use_awaitable);
    }
    make_shared<ProxyServer::Session>(move(client), move(remote))->start();
}

ProxyServer::Session::Session(tcp::socket client, tcp::socket remote)
    : client{move(client)}
    , remote{move(remote)}
{
}

void ProxyServer::Session::start()
{
    auto read = [self = shared_from_this()]() {
        return self->reader();
    };
    auto write = [self = shared_from_this()]() {
        return self->writer();
    };
    asio::co_spawn(client.get_executor(), read, asio::detached);
    asio::co_spawn(client.get_executor(), write, asio::detached);
}

void ProxyServer::Session::stop()
{
    client.close();
    remote.close();
}

asio::awaitable<void> ProxyServer::Session::reader()
{
    string buf(1024, '\0');
    try {
        while (true) {
            auto n = co_await client.async_read_some(asio::buffer(buf), asio::use_awaitable);
            co_await asio::async_write(remote, asio::buffer(buf, n), asio::use_awaitable);
        }
    } catch (exception&) {
        stop();
    }
}

asio::awaitable<void> ProxyServer::Session::writer()
{
    string buf(1024, '\0');
    try {
        while (true) {
            auto n = co_await remote.async_read_some(asio::buffer(buf), asio::use_awaitable);
            co_await asio::async_write(client, asio::buffer(buf, n), asio::use_awaitable);
        }
    } catch (exception&) {
        stop();
    }
}
