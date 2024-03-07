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

This project uses C++20 coroutine feature, so `g++-10` or a higher version is required to compile.

## Usage

We can either specify an upstream server, or a PAC file.

- Always use the specified upstream server:

    `krbproxy -s proxy.mycompany.net:8080`

- Automatically choose proxy servers with a PAC file:

    `krbproxy -p /path/to/proxy.pac`

- Listen on a difference address rather than the default (`127.0.0.1:3128`):

    `krbproxy -p /path/to/proxy.pac -l 192.168.1.100:8888`

## Setup Kerberos environment

We need to correctly setup the Kerberos environment on our Operating System, if we use Krbproxy in a corporate network.

### Find out Kerberos realm

If you have a Windows computer which has been joined in the company's AD domain, we can find the realm in computer name.

For example, my computer name is `LAPTOP123456.europe.mycompany.corp`, then the realm should be `EUROPE.MYCOMPANY.CORP`.

### Automatic login

It's recommended to use a client keytab file to save your company account password in an encrypted format.

Install `krb5-user` package and follow [ktutil documentation](https://web.mit.edu/kerberos/krb5-1.12/doc/admin/admin_commands/ktutil.html) to create a keytab file.

Edit `/etc/krb5.conf`, config `default_realm` and `default_client_keytab_name` in `libdefaults` section. You may also want to config `default_ccache_name` to keep your credential cache after system reboot. See [krb5.conf documentation](https://web.mit.edu/kerberos/krb5-1.12/doc/admin/conf_files/krb5_conf.html) for more information.

## Note

- Krbproxy skips authentication for IP addresses and plain hosts. This is useful if the PAC file returns more than one proxy server but some of them do not require authentication. 

- If PAC file returns more than one proxy for a URL, Krbproxy uses the first one and ignores others. (Yes, the author is lazy and chose the easiest implementation).

- Krbproxy does not support upstream SOCKS proxy.

See example PAC file below:

```
function FindProxyForURL(url, host) {
    if (/(?:^|\.)pattern1\.com$/.test(host)) return "PROXY proxy2.mycompany.net:8080; PROXY 10.0.3.6:8080"; // Use "PROXY proxy2.mycompany.net:8080" and ignore "PROXY 10.0.3.6:8080"
    if (/(?:^|\.)pattern2\.com$/.test(host)) return "DIRECT"; // OK, direct connection is supported
    if (/(?:^|\.)pattern3\.com$/.test(host)) return "SOCKS5 127.0.0.1:1080"; // No, SOCKS proxy is not supported
    if (/(?:^|\.)pattern4\.com$/.test(host)) return "PROXY localhost:8080"; // Disable authentication (Plain host)
    if (/(?:^|\.)pattern5\.com$/.test(host)) return "PROXY 10.0.2.8:8080"; // Disable authentication (IP address)
    return "PROXY proxy.mycompany.net:8080"; // Enable authentication (Domain name)
}
```
