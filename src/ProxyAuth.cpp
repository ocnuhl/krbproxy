#include "ProxyAuth.h"

using namespace std;

struct ProxyAuth::Data {
};

ProxyAuth::ProxyAuth()
    : d_ptr{make_unique<Data>()}
{
}

ProxyAuth::~ProxyAuth()
{
}

std::string_view ProxyAuth::getAuthHeader()
{
    return "Proxy-Authorization: Negotiate xxx\r\n"sv;
}
