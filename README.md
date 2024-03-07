# Krbproxy

Krbproxy is an HTTP proxy server that can authenticate to upstream HTTP proxy servers with Kerberos.

This kind of upstream HTTP proxy server is often deployed in a corporate network environment, and usually provides several authentication schemes, such as Basic, NTLM, Negotiate. Krbproxy only supports Kerberos, which is a more secure one.

Krbproxy can also be used as an HTTP proxy server with PAC support, even if it's not running in a corporate network that requires Kerberos.

Currently Krbproxy only supports Linux. I have tested on Debian 11 and Ubuntu 20.04. It may also work on other distributions.

## Installation

Install runtime dependencies. For Debian or Ubuntu, simply run:

`sudo apt install libgssapi-krb5-2 libpacparser1`

Then download the latest binary from release page.

It can also be compiled from source. The build dependencies are `cmake g++ make libpacparser-dev libkrb5-dev libboost-dev`.

This project uses C++20 coroutine feature, so g++-10 or a higher version is required to compile.

## Usage

We can either specify an upstream server, or a PAC file.

- Always use the specified upstream server:

    `krbproxy -s proxy.mycompany.net:8080`

- Automatically choose proxy servers with a PAC file:

    `krbproxy -p /path/to/proxy.pac`

- Listen on a difference address rather than the default (127.0.0.1:3128):

    `krbproxy -p /path/to/proxy.pac -l 192.168.1.100:8888`
