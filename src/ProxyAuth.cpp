#include "ProxyAuth.h"

#include <boost/beast/core/detail/base64.hpp>
#include <gssapi/gssapi.h>
#include <string>

using namespace std;
using namespace boost::beast::detail::base64;

struct ProxyAuth::Data {
    string authHeader;
};

ProxyAuth::ProxyAuth()
    : d_ptr{make_unique<Data>()}
{
}

ProxyAuth::~ProxyAuth()
{
}

string_view ProxyAuth::getAuthHeader(string_view host)
{
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
    auto size = encoded_size(output_token.length) + 33;
    d_ptr->authHeader.resize(size);
    encode(d_ptr->authHeader.data() + 31, output_token.value, output_token.length);
    d_ptr->authHeader.replace(0, 31, "Proxy-Authorization: Negotiate ");
    d_ptr->authHeader.replace(size - 2, 2, "\r\n");
    gss_release_buffer(&minor, &output_token);
    gss_delete_sec_context(&minor, &ctx, NULL);
    gss_release_name(&minor, &target_name);
    return d_ptr->authHeader.size() == 33 ? string_view{} : d_ptr->authHeader;
}
