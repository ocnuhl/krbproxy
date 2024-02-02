#pragma once

#include <memory>
#include <string>
#include <string_view>

struct Server {
    std::string_view host;
    std::string_view port;
    bool empty() const { return host.empty() || port.empty(); }
    static Server fromString(std::string_view str);
};

class ProxyFinder
{
public:
    ProxyFinder();
    ~ProxyFinder();
    void setPacFile(const std::string& pacFile);
    Server findProxyForHost(const std::string& host);

private:
    struct Data;
    std::unique_ptr<Data> d_ptr;
};
