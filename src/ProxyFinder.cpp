#include "ProxyFinder.h"

#include <pacparser.h>

using namespace std;

Server Server::fromString(string_view str)
{
    Server server{};
    if (!str.empty()) {
        auto pos = str.rfind(':');
        if (pos > 0 && pos < str.size() - 1) {
            server.host = str.substr(0, pos);
            server.port = str.substr(pos + 1);
        }
    }
    return server;
}

struct ProxyFinder::Data {
    bool initialized;
};

ProxyFinder::ProxyFinder()
    : d_ptr{make_unique<Data>()}
{
}

ProxyFinder::~ProxyFinder()
{
    if (d_ptr->initialized) {
        pacparser_cleanup();
    }
}

void ProxyFinder::setPacFile(const string& pacFile)
{
    if (d_ptr->initialized) {
        return;
    }
    pacparser_init();
    pacparser_parse_pac_file(pacFile.c_str());
    d_ptr->initialized = true;
}

Server ProxyFinder::findProxyForHost(const string& host)
{
    if (!d_ptr->initialized || host.empty()) {
        return {};
    }
    string_view str = pacparser_find_proxy(host.c_str(), host.c_str());
    if (str.starts_with("PROXY ")) {
        str.remove_prefix(6);
        auto pos = str.find_first_of(";");
        return Server::fromString(str.substr(0, pos));
    }
    return {};
}
