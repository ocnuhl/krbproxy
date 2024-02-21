#include "ProxyServer.h"

#include <boost/asio.hpp>
#include <iostream>
#include <regex>
#include <vector>

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
    asio::awaitable<void> readClient();
    asio::awaitable<void> readRemote();
    asio::awaitable<void> processHeadProxied();
    asio::awaitable<void> processHeadDirectConnect();
    asio::awaitable<void> processHeadDirectOther();
    asio::awaitable<void> flushAndRefill(vector<asio::const_buffer>&, string_view&);
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
    if (!config.pac.empty())
        d_ptr->proxyFinder.setPacFile(string{config.pac});
}

ProxyServer::~ProxyServer()
{
}

void ProxyServer::run()
{
    asio::io_context context;
    asio::signal_set signals(context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { context.stop(); });
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
    if (host.empty())
        co_return;
    Server proxy = defaultProxy;
    if (proxy.empty())
        proxy = proxyFinder.findProxyForHost(host);
    Server target = proxy;
    if (target.empty())
        target = {host, port};
    tcp::resolver resolver{client.get_executor()};
    tcp::socket remote{client.get_executor()};
    auto result = resolver.resolve(target.host, target.port);
    co_await asio::async_connect(remote, result, asio::use_awaitable);
    make_shared<ProxyServer::Session>(move(client), move(remote), move(buf), proxy)->start();
}

pair<string, string> ProxyServer::Data::parseRequestUrl(const string& buf)
{
    string host, port;
    auto pattern = "^\\w+ http://([^:/]+)(?::(\\d+))?/";
    if (buf.starts_with("CONNECT"))
        pattern = "CONNECT ([^:]+):(\\d+) HTTP";
    smatch match;
    regex_search(buf, match, regex{pattern});
    if (match.size() == 3) {
        host = match[1];
        port = match[2];
    }
    if (host.empty())
        cerr << "Bad request: " << buf << endl;
    if (port.empty())
        port = "80";
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
    client.set_option(tcp::no_delay{true});
    remote.set_option(tcp::no_delay{true});
    auto pipe1 = [self = shared_from_this()]() { return self->readClient(); };
    auto pipe2 = [self = shared_from_this()]() { return self->readRemote(); };
    asio::co_spawn(client.get_executor(), pipe1, asio::detached);
    asio::co_spawn(client.get_executor(), pipe2, asio::detached);
}

void ProxyServer::Session::stop()
{
    client.close();
    remote.close();
}

asio::awaitable<void> ProxyServer::Session::readClient()
{
    try {
        if (proxy.empty())
            if (readBuf.starts_with("CONNECT"))
                co_await processHeadDirectConnect();
            else
                co_await processHeadDirectOther();
        else
            co_await processHeadProxied();
        readBuf.resize(1024);
        while (true) {
            auto n = co_await client.async_read_some(asio::buffer(readBuf), asio::use_awaitable);
            co_await asio::async_write(remote, asio::buffer(readBuf, n), asio::use_awaitable);
        }
    } catch (exception&) {
        stop();
    }
}

asio::awaitable<void> ProxyServer::Session::readRemote()
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
    vector<asio::const_buffer> bufs;
    string_view bufView = readBuf;
    string_view line;
    bool partialLine = false;
    while (true) {
        if (bufView.empty())
            co_await flushAndRefill(bufs, bufView);
        line = readline(bufView);
        // "\r\n" or "\r". "\n" will be handled by readClient()
        if (line.starts_with('\r') && !partialLine)
            break;
        bufs.push_back(asio::buffer(line));
        partialLine = !line.ends_with('\n');
    }
    string authHeader = proxyAuth.getAuthHeader(proxy.host);
    bufs.push_back(asio::buffer(authHeader));
    bufs.push_back(asio::buffer(line));
    bufs.push_back(asio::buffer(bufView));
    co_await asio::async_write(remote, bufs, asio::use_awaitable);
}

asio::awaitable<void> ProxyServer::Session::processHeadDirectConnect()
{
    vector<asio::const_buffer> bufs;
    string_view bufView = readBuf;
    bool partialLine = false;
    while (true) {
        if (bufView.empty())
            co_await flushAndRefill(bufs, bufView);
        string_view line = readline(bufView);
        // Must be "\r\n". "\r" without "\n" is now allowed.
        if (line == "\r\n" && !partialLine)
            break;
        partialLine = !line.ends_with('\n');
    }
    co_await asio::async_write(client, asio::buffer("HTTP/1.1 200 Connection Established\r\n\r\n"sv), asio::use_awaitable);
}

asio::awaitable<void> ProxyServer::Session::processHeadDirectOther()
{
    vector<asio::const_buffer> bufs;
    string_view bufView = readBuf;
    string_view line = readline(bufView);
    bool partialLine = !line.ends_with('\n');
    bool skipWrite = false;

    string request;
    regex_replace(back_inserter(request), line.begin(), line.end(), regex{"http://[^/]+/"}, "/");
    bufs.push_back(asio::buffer(request));

    while (true) {
        if (bufView.empty())
            co_await flushAndRefill(bufs, bufView);
        line = readline(bufView);
        // "\r\n" or "\r". "\n" will be handled by readClient()
        if (line.starts_with('\r') && !partialLine)
            break;
        if (!partialLine)
            skipWrite = line.starts_with("Proxy-");
        partialLine = !line.ends_with('\n');
        if (!skipWrite)
            bufs.push_back(asio::buffer(line));
    }
    bufs.push_back(asio::buffer(line));
    bufs.push_back(asio::buffer(bufView));
    co_await asio::async_write(remote, bufs, asio::use_awaitable);
}

asio::awaitable<void> ProxyServer::Session::flushAndRefill(vector<asio::const_buffer>& bufs, string_view& bufView)
{
    if (!bufs.empty()) {
        co_await asio::async_write(remote, bufs, asio::use_awaitable);
        bufs.clear();
    }
    readBuf.resize(1024);
    auto n = co_await client.async_read_some(asio::buffer(readBuf), asio::use_awaitable);
    bufView = string_view{readBuf.data(), n};
}

string_view ProxyServer::Session::readline(string_view& sv)
{
    auto pos = sv.find('\n');
    if (pos == sv.npos)
        --pos;
    string_view line = sv.substr(0, pos + 1);
    sv.remove_prefix(line.size());
    return line;
}
