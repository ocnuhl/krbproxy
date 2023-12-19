#pragma once

#include <memory>
#include <string>

class ProxyServer
{
public:
    struct Config {
        std::string server;
        std::string pac;
        std::string listen;
    };
    ProxyServer(const Config& config);
    ~ProxyServer();
    void run();

private:
    struct Data;
    std::unique_ptr<Data> d_ptr;
};
