#include <bearssl.h>
#include "pinned_key.h"

#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <memory>

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  signal(SIGPIPE, SIG_IGN);
  std::ifstream file(argv[2], std::ios::binary);
  const std::vector<unsigned char> key{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  br_pkey_decoder_context decoder;
  br_pkey_decoder_init(&decoder);
  br_pkey_decoder_push(&decoder, key.data(), key.size());
  if (br_pkey_decoder_last_error(&decoder)) return 3;
  br_x509_knownkey_context pinned;
  std::unique_ptr<unsigned char, decltype(&std::free)> point(nullptr, &std::free);
  if (br_pkey_decoder_key_type(&decoder) == BR_KEYTYPE_RSA)
    br_x509_knownkey_init_rsa(&pinned, br_pkey_decoder_get_rsa(&decoder), BR_KEYTYPE_SIGN | BR_KEYTYPE_KEYX);
  else if (br_pkey_decoder_key_type(&decoder) == BR_KEYTYPE_EC) {
    br_ec_public_key ec = *br_pkey_decoder_get_ec(&decoder);
    auto* copy = static_cast<unsigned char*>(std::malloc(ec.qlen));
    if (!copy) return 4;
    std::memcpy(copy, ec.q, ec.qlen);
    ec.q = copy;
    if (!normalizeOwnedPinnedEcPoint(ec)) { std::free(copy); return 4; }
    point.reset(ec.q);
    if (!normalizeOwnedPinnedEcPoint(ec)) return 4;  // Idempotent for already-correct points.
    br_x509_knownkey_init_ec(&pinned, &ec, BR_KEYTYPE_SIGN | BR_KEYTYPE_KEYX);
  }
  else return 4;
  br_ssl_client_context client;
  br_x509_minimal_context unusedCa;
  br_ssl_client_init_full(&client, &unusedCa, nullptr, 0);
  // This is the same authenticator installed by ESP8266 WiFiClientSecure::setKnownKey.
  br_ssl_engine_set_x509(&client.eng, &pinned.vtable);
  unsigned char buffer[BR_SSL_BUFSIZE_BIDI];
  br_ssl_engine_set_buffer(&client.eng, buffer, sizeof(buffer), 1);
  if (!br_ssl_client_reset(&client, "pinned.nightscout.invalid", 0)) return 5;
  const int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return 6;
  const timeval timeout{5, 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<unsigned short>(std::atoi(argv[1])));
  if (connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) { close(sock); return 7; }
  int result = 8;
  bool requestSent = false;
  for (unsigned step = 0; step < 10000; ++step) {
    const unsigned state = br_ssl_engine_current_state(&client.eng);
    if (state & BR_SSL_CLOSED) break;
    size_t size = 0;
    if (state & BR_SSL_SENDREC) {
      unsigned char* bytes = br_ssl_engine_sendrec_buf(&client.eng, &size);
      const ssize_t count = send(sock, bytes, size, 0);
      if (count <= 0) break;
      br_ssl_engine_sendrec_ack(&client.eng, static_cast<size_t>(count));
    } else if ((state & BR_SSL_SENDAPP) && !requestSent) {
      // SENDAPP is only exposed after authenticating the TLS handshake.
      const char request[] = "GET / HTTP/1.0\r\nHost: pinned.nightscout.invalid\r\n\r\n";
      unsigned char* bytes = br_ssl_engine_sendapp_buf(&client.eng, &size);
      if (size < sizeof(request) - 1) break;
      std::memcpy(bytes, request, sizeof(request) - 1);
      br_ssl_engine_sendapp_ack(&client.eng, sizeof(request) - 1);
      br_ssl_engine_flush(&client.eng, 0);
      requestSent = true;
    } else if (state & BR_SSL_RECVAPP) {
      unsigned char* bytes = br_ssl_engine_recvapp_buf(&client.eng, &size);
      if (size >= 12 && std::memcmp(bytes, "HTTP/1.0 200", 12) == 0) result = 0;
      break;
    } else if (state & BR_SSL_RECVREC) {
      unsigned char* bytes = br_ssl_engine_recvrec_buf(&client.eng, &size);
      const ssize_t count = recv(sock, bytes, size, 0);
      if (count <= 0) break;
      br_ssl_engine_recvrec_ack(&client.eng, static_cast<size_t>(count));
    } else break;
  }
  close(sock);
  if (result) std::fprintf(stderr, "TLS error=%d request_sent=%d\n", br_ssl_engine_last_error(&client.eng), requestSent);
  return result;
}
