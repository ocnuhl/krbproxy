#pragma once

#include <memory>
#include <string_view>

class ProxyAuth
{
public:
    ProxyAuth();
    ~ProxyAuth();
    std::string_view getAuthHeader();

private:
    struct Data;
    std::unique_ptr<Data> d_ptr;
};
