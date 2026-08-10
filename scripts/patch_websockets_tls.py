Import("env")

from hashlib import sha256
from pathlib import Path


source = (
    Path(env.subst("$PROJECT_LIBDEPS_DIR"))
    / env.subst("$PIOENV")
    / "WebSockets"
    / "src"
    / "WebSocketsClient.cpp"
)
original_hash = "00c7740ae0d18f293f65ea03ca6124986f4a5292d1aef41ba020e877f2ef7eb7"
marker = "FLOVA_ESP8266_LINK_TLS_RX_BYTES"
legacy_marker = "FLOVA_ESP8266_WSS_TLS_RX_BYTES"
cleanup_marker = "FLOVA_CLIENT_DISCONNECTS_BEARSSL_UNCONDITIONALLY"
string_cleanup_marker = "FLOVA_CLIENT_RELEASES_HANDSHAKE_STRINGS"
clock_marker = "FLOVA_CLIENT_SETS_X509_TIME"
error_marker = "FLOVA_CLIENT_REPORTS_TLS_ERRORS"
needle = """            _client.ssl = new WEBSOCKETS_NETWORK_SSL_CLASS();
            _client.tcp = _client.ssl;
"""
replacement = """            _client.ssl = new WEBSOCKETS_NETWORK_SSL_CLASS();
#if defined(ESP8266) && defined(SSL_BARESSL)
            _client.ssl->setBufferSizes(FLOVA_ESP8266_LINK_TLS_RX_BYTES,
                                        FLOVA_ESP8266_TLS_TX_BYTES);
#endif
            _client.tcp = _client.ssl;
"""

if not source.exists():
    raise RuntimeError(f"WebSockets source not found: {source}")

raw = source.read_bytes()
contents = raw.decode().replace("\r\n", "\n")
if legacy_marker in contents:
    source.write_text(contents.replace(legacy_marker, marker))
elif marker not in contents:
    digest = sha256(raw).hexdigest()
    if digest != original_hash:
        raise RuntimeError(
            "WebSockets@2.7.3 source changed; review the ESP8266 TLS buffer patch "
            f"before building ({digest})"
        )
    if contents.count(needle) != 1:
        raise RuntimeError("WebSockets TLS allocation hook was not found exactly once")
    contents = contents.replace(needle, replacement)

if cleanup_marker not in contents:
    disconnect_needle = """void WebSocketsClient::disconnect(void) {
    if(clientIsConnected(&_client)) {
        WebSockets::clientDisconnect(&_client, 1000);
    }
}
"""
    legacy_disconnect = """void WebSocketsClient::disconnect(void) {
    // WebSocketsClient::begin() overwrites _client.ssl.  Release a TLS object
    // even when the handshake failed before clientIsConnected() became true.
    // Without this, every bootstrap retry leaks one BearSSL context.
    if(clientIsConnected(&_client) || _client.ssl || _client.tcp) {
        WebSocketsClient::clientDisconnect(&_client, NULL);
    }
    // FLOVA_CLIENT_DISCONNECT_CLEANS_FAILED_TLS
}
"""
    disconnect_replacement = """void WebSocketsClient::disconnect(void) {
    // Release TLS/TCP state even when the handshake failed before
    // clientIsConnected() became true.
    if(clientIsConnected(&_client) || _client.ssl || _client.tcp) {
        WebSocketsClient::clientDisconnect(&_client, NULL);
    }
    // FLOVA_CLIENT_DISCONNECTS_BEARSSL_UNCONDITIONALLY
}
"""
    if legacy_disconnect in contents:
        contents = contents.replace(legacy_disconnect, disconnect_replacement)
    elif contents.count(disconnect_needle) == 1:
        contents = contents.replace(disconnect_needle, disconnect_replacement)
    else:
        raise RuntimeError("WebSockets disconnect hook was not found exactly once")

if string_cleanup_marker not in contents:
    # The actual condition is checked below using the stable body, because the
    # library has used both client->isSSL and client->ssl guards across minor
    # releases.
    ssl_body_needle = """        if(client->ssl->connected()) {
            client->ssl->flush();
            client->ssl->stop();
        }
        event = true;
        delete client->ssl;
"""
    ssl_body_replacement = """        if(client->ssl->connected()) {
            client->ssl->flush();
        }
        // BearSSL::WiFiClientSecure::stop() also frees the TLS engine and
        // socket context when connected() is already false.  Skipping it on
        // failed handshakes fragments the ESP8266 heap across retries.
        client->ssl->stop();
        event = true;
        delete client->ssl;
"""
    if contents.count(ssl_body_needle) != 1:
        raise RuntimeError("WebSockets BearSSL cleanup body was not found exactly once")
    contents = contents.replace(ssl_body_needle, ssl_body_replacement)

    tcp_body_needle = """        if(client->tcp->connected()) {
#if (WEBSOCKETS_NETWORK_TYPE != NETWORK_ESP8266_ASYNC)
            client->tcp->flush();
#endif
            client->tcp->stop();
        }
        event = true;
"""
    tcp_body_replacement = """        if(client->tcp->connected()) {
#if (WEBSOCKETS_NETWORK_TYPE != NETWORK_ESP8266_ASYNC)
            client->tcp->flush();
#endif
        }
        client->tcp->stop();
        event = true;
"""
    if contents.count(tcp_body_needle) != 1:
        raise RuntimeError("WebSockets TCP cleanup body was not found exactly once")
    contents = contents.replace(tcp_body_needle, tcp_body_replacement)

    string_cleanup_needle = """    client->cCode        = 0;
    client->cKey         = "";
    client->cAccept      = "";
    client->cVersion     = 0;
    client->cIsUpgrade   = false;
    client->cIsWebsocket = false;
    client->cSessionId   = "";

    client->status      = WSC_NOT_CONNECTED;
"""
    string_cleanup_replacement = """    client->cCode        = 0;
    // Move-assigning an empty String frees the old heap buffer.  Assignment
    // from \"\" only resets the length and retains the capacity.
    client->cKey         = String();
    client->cAccept      = String();
    client->cExtensions  = String();
    client->cSessionId   = String();
#if (WEBSOCKETS_NETWORK_TYPE == NETWORK_ESP8266_ASYNC)
    client->cHttpLine    = String();
#endif
    client->cVersion     = 0;
    client->cIsUpgrade   = false;
    client->cIsWebsocket = false;

    client->status      = WSC_NOT_CONNECTED;
    // FLOVA_CLIENT_RELEASES_HANDSHAKE_STRINGS
"""
    if contents.count(string_cleanup_needle) != 1:
        raise RuntimeError("WebSockets handshake cleanup fields were not found exactly once")
    contents = contents.replace(string_cleanup_needle, string_cleanup_replacement)

if clock_marker not in contents:
    buffer_hook = """            _client.ssl->setBufferSizes(FLOVA_ESP8266_LINK_TLS_RX_BYTES,
                                        FLOVA_ESP8266_TLS_TX_BYTES);
"""
    clock_hook = buffer_hook + """            if (time(nullptr) > 1700000000) {
                _client.ssl->setX509Time(time(nullptr));
            }
            // FLOVA_CLIENT_SETS_X509_TIME
"""
    if contents.count(buffer_hook) != 1:
        raise RuntimeError("WebSockets TLS clock hook was not found exactly once")
    contents = contents.replace(buffer_hook, clock_hook)

if error_marker not in contents:
    failed_needle = """void WebSocketsClient::connectFailedCb() {
    DEBUG_WEBSOCKETS(\"[WS-Client] connection to %s:%u Failed\\n\", _host.c_str(), _port);
}
"""
    failed_replacement = """void WebSocketsClient::connectFailedCb() {
    DEBUG_WEBSOCKETS(\"[WS-Client] connection to %s:%u Failed\\n\", _host.c_str(), _port);
#if defined(ESP8266) && defined(SSL_BARESSL)
    char detail[96] = {};
    const int errorCode = _client.ssl ? _client.ssl->getLastSSLError(detail, sizeof(detail)) : 0;
    char reason[128] = {};
    const char* category = errorCode == 0 ? \"network_connect_failed\" : \"tls_connect_failed\";
    snprintf(reason, sizeof(reason), \"%s code=%d detail=%.*s\", category, errorCode, 80, detail);
    runCbEvent(WStype_ERROR, reinterpret_cast<uint8_t*>(reason), strlen(reason));
#endif
    // FLOVA_CLIENT_REPORTS_TLS_ERRORS
}
"""
    if contents.count(failed_needle) != 1:
        raise RuntimeError("WebSockets TLS error hook was not found exactly once")
    contents = contents.replace(failed_needle, failed_replacement)

for legacy_error_line in (
    '    snprintf(reason, sizeof(reason), "tls_connect_failed code=%d detail=%.*s", errorCode, 80, detail);',
    '    snprintf(reason, sizeof(reason), "tls_connect_failed code=%d detail=%s", errorCode, detail);',
):
    if legacy_error_line in contents:
        contents = contents.replace(
            legacy_error_line,
            '    const char* category = errorCode == 0 ? "network_connect_failed" : "tls_connect_failed";\n'
            '    snprintf(reason, sizeof(reason), "%s code=%d detail=%.*s", category, errorCode, 80, detail);',
        )

source.write_text(contents)
