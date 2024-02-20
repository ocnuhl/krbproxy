#include "ProxyServer.h"

#include <cstring>
#include <iostream>

using namespace std;

void usage()
{
    cout << "krbproxy 1.0.0\n"
            "Usage: krbproxy [OPTION]...\n\n"
            "  -s, --server    upstream proxy server address\n"
            "  -p, --pac       local PAC file location\n"
            "  -l, --listen    proxy server listen address. default: 127.0.0.1:3128\n"
            "  -h, --help      print this help\n";
}

int main(int argc, char* argv[])
{
    ProxyServer::Config config{};
    for (int i = 1; i + 1 < argc; i += 2) {
        auto option = argv[i];
        auto value = argv[i + 1];
        if (strcmp(option, "-s") == 0 || strcmp(option, "--server") == 0) {
            config.server = value;
        } else if (strcmp(option, "-p") == 0 || strcmp(option, "--pac") == 0) {
            config.pac = value;
        } else if (strcmp(option, "-l") == 0 || strcmp(option, "--listen") == 0) {
            config.listen = value;
        }
    }
    if (config.server.empty() && config.pac.empty()) {
        usage();
        return 0;
    }
    ProxyServer proxyServer{config};
    proxyServer.run();
    return 0;
}
