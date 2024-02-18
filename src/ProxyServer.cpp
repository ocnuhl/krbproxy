#include "ProxyServer.h"

#include <boost/asio.hpp>
#include <iostream>
#include <regex>
#include <utility>

#include "ProxyAuth.h"
#include "ProxyFinder.h"

using namespace std;
using boost::asio::ip::tcp;
namespace asio = boost::asio;

struct ProxyServer::Data {
    Server defaultProxy;
    Server localServer;
    ProxyFinder proxyFinder;
    asio::awaitable<void> startServer();
    asio::awaitable<void> serveClient(tcp::socket client);
    pair<string, string> parseRequestUrl(const string& buf);
};

class ProxyServer::Session : public enable_shared_from_this<ProxyServer::Session>
{
public:
    Session(tcp::socket client, tcp::socket remote, string readBuf, Server proxy);
    void start();

private:
    void stop();
    asio::awaitable<void> reader();
    asio::awaitable<void> writer();
    asio::awaitable<void> processHeadProxied();
    asio::awaitable<void> processHeadDirectConnect();
    asio::awaitable<void> processHeadDirectOther();
    string_view readline(string_view& sv);
    tcp::socket client;
    tcp::socket remote;
    string readBuf;
    Server proxy;
    ProxyAuth proxyAuth;
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
    auto n = co_await client.async_read_some(asio::buffer(buf), asio::use_awaitable);
    buf.resize(n);
    auto [host, port] = parseRequestUrl(buf);
    if (host.empty()) {
        co_return;
    }
    Server proxy = defaultProxy;
    if (proxy.empty()) {
        proxy = proxyFinder.findProxyForHost(host);
    }
    Server target = proxy;
    if (target.empty()) {
        target = {host, port};
    }
    tcp::resolver resolver{client.get_executor()};
    tcp::socket remote{client.get_executor()};
    auto result = resolver.resolve(target.host, target.port);
    co_await remote.async_connect(*result, asio::use_awaitable);
    make_shared<ProxyServer::Session>(move(client), move(remote), move(buf), proxy)->start();
}

pair<string, string> ProxyServer::Data::parseRequestUrl(const string& buf)
{
    string host, port;
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
    }
    if (port.empty()) {
        port = "80";
    }
    return {host, port};
}

ProxyServer::Session::Session(tcp::socket client, tcp::socket remote, string readBuf, Server proxy)
    : client{move(client)}
    , remote{move(remote)}
    , readBuf{move(readBuf)}
    , proxy{proxy}
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
    try {
        if (proxy.empty()) {
            if (readBuf.starts_with("CONNECT")) {
                co_await processHeadDirectConnect();
            } else {
                co_await processHeadDirectOther();
            }
        } else {
            co_await processHeadProxied();
        }
        readBuf.resize(1024);
        while (true) {
            auto n = co_await client.async_read_some(asio::buffer(readBuf), asio::use_awaitable);
            co_await asio::async_write(remote, asio::buffer(readBuf, n), asio::use_awaitable);
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

asio::awaitable<void> ProxyServer::Session::processHeadProxied()
{
    string_view bufview = readBuf;
    string_view line;
    bool partialLine = false;
    while (true) {
        if (bufview.empty()) {
            readBuf.resize(1024);
            auto n = co_await client.async_read_some(asio::buffer(readBuf), asio::use_awaitable);
            bufview = string_view{readBuf.data(), n};
        }
        line = readline(bufview);
        if (!partialLine) {
            if (line.starts_with('\r')) {
                break;
            }
        }
        co_await asio::async_write(remote, asio::buffer(line), asio::use_awaitable);
        partialLine = !line.ends_with('\n');
    }
    string_view authHeader = proxyAuth.getAuthHeader(proxy.host);
    if (!authHeader.empty()) {
        co_await asio::async_write(remote, asio::buffer(authHeader), asio::use_awaitable);
    }
    co_await asio::async_write(remote, asio::buffer(line), asio::use_awaitable);
    if (!bufview.empty()) {
        co_await asio::async_write(remote, asio::buffer(bufview), asio::use_awaitable);
    }
}

asio::awaitable<void> ProxyServer::Session::processHeadDirectConnect()
{
    string_view bufview = readBuf;
    bool partialLine = false;
    while (true) {
        if (bufview.empty()) {
            readBuf.resize(1024);
            auto n = co_await client.async_read_some(asio::buffer(readBuf), asio::use_awaitable);
            bufview = string_view{readBuf.data(), n};
        }
        string_view line = readline(bufview);
        if (!partialLine) {
            if (line == "\r\n" || line == "\n") {
                break;
            }
        }
        partialLine = !line.ends_with('\n');
    }
    co_await asio::async_write(client, asio::buffer("HTTP/1.1 200 Connection Established\r\n\r\n"sv), asio::use_awaitable);
}

asio::awaitable<void> ProxyServer::Session::processHeadDirectOther()
{
    string_view bufview = readBuf;
    string_view line = readline(bufview);
    string request;
    regex_replace(back_inserter(request), line.begin(), line.end(), regex{"http://[^/]+/"}, "/");
    co_await asio::async_write(remote, asio::buffer(request), asio::use_awaitable);
    bool partialLine = !line.ends_with('\n');
    bool skipWrite = false;
    while (true) {
        if (bufview.empty()) {
            readBuf.resize(1024);
            auto n = co_await client.async_read_some(asio::buffer(readBuf), asio::use_awaitable);
            bufview = string_view{readBuf.data(), n};
        }
        line = readline(bufview);
        if (!partialLine) {
            if (line == "\r\n" || line == "\n") {
                break;
            }
            skipWrite = line.starts_with("Proxy-");
        }
        partialLine = !line.ends_with('\n');
        if (!skipWrite) {
            co_await asio::async_write(remote, asio::buffer(line), asio::use_awaitable);
        }
    }
    co_await asio::async_write(remote, asio::buffer(line), asio::use_awaitable);
    if (!bufview.empty()) {
        co_await asio::async_write(remote, asio::buffer(bufview), asio::use_awaitable);
    }
}

string_view ProxyServer::Session::readline(string_view& sv)
{
    auto pos = sv.find('\n');
    if (pos == sv.npos) {
        --pos;
    }
    string_view line = sv.substr(0, pos + 1);
    sv.remove_prefix(line.size());
    return line;
}
