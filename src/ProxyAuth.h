#pragma once

#include <string>
#include <string_view>

class ProxyAuth
{
public:
    std::string getAuthHeader(std::string_view host);

private:
    bool skipAuth(std::string_view host);
};
