#pragma once

#include <memory>
#include <string_view>

class ProxyServer
{
public:
    struct Config {
        std::string_view server;
        std::string_view pac;
        std::string_view listen;
    };
    ProxyServer(const Config& config);
    ~ProxyServer();
    void run();

private:
    struct Data;
    std::unique_ptr<Data> d_ptr;
};
