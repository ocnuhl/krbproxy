#include "ProxyAuth.h"

#include <boost/beast/core/detail/base64.hpp>
#include <cctype>

#ifdef __APPLE__
#include <GSS/gssapi.h>
#else
#include <gssapi/gssapi.h>
#endif

using namespace std;
using namespace boost::beast::detail::base64;

string ProxyAuth::getAuthHeader(string_view host)
{
    string authHeader;
    if (skipAuth(host)) {
        return authHeader;
    }
    gss_ctx_id_t ctx = GSS_C_NO_CONTEXT;
    OM_uint32 minor, ret_flags;
    gss_buffer_desc output_token = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc name_buf = GSS_C_EMPTY_BUFFER;
    gss_name_t target_name = GSS_C_NO_NAME;
    string name = "HTTP@" + string{host};
    name_buf.value = name.data();
    name_buf.length = name.size();
    gss_import_name(&minor, &name_buf, GSS_C_NT_HOSTBASED_SERVICE, &target_name);
    gss_init_sec_context(&minor, GSS_C_NO_CREDENTIAL, &ctx, target_name, GSS_C_NO_OID, 0,
                         0, NULL, GSS_C_NO_BUFFER, NULL, &output_token, &ret_flags, NULL);
    if (output_token.length > 0) {
        auto size = encoded_size(output_token.length) + 33;
        authHeader.resize(size);
        encode(authHeader.data() + 31, output_token.value, output_token.length);
        authHeader.replace(0, 31, "Proxy-Authorization: Negotiate ");
        authHeader.replace(size - 2, 2, "\r\n");
    }
    gss_release_buffer(&minor, &output_token);
    gss_delete_sec_context(&minor, &ctx, NULL);
    gss_release_name(&minor, &target_name);
    return authHeader;
}

bool ProxyAuth::skipAuth(string_view host)
{
    bool hasAlpha = false;
    for (char c : host) {
        if (isalpha(static_cast<unsigned char>(c))) {
            hasAlpha = true;
        } else if (c == '.' && hasAlpha) {
            return false; // Normal domain name
        }
    }
    return true; // ipv4, ipv6, or plain host
}
