#include "public_ca_store.h"

#include <assert.h>
#include <fstream>
#include <iterator>
#include <stdio.h>
#include <time.h>
#include <vector>

std::vector<unsigned char> read(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  assert(stream.good());
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

unsigned verify(const std::vector<unsigned char>& certificate, const char* name) {
  PublicCaStore store;
  br_x509_minimal_context context;
  br_x509_minimal_init_full(&context, nullptr, 0);
  store.installCertStore(&context);
  const time_t now = time(nullptr);
  br_x509_minimal_set_time(&context, static_cast<uint32_t>(now / 86400 + 719528),
                          static_cast<uint32_t>(now % 86400));
  const br_x509_class** chain = &context.vtable;
  (*chain)->start_chain(chain, name);
  (*chain)->start_cert(chain, certificate.size());
  (*chain)->append(chain, certificate.data(), certificate.size());
  (*chain)->end_cert(chain);
  return (*chain)->end_chain(chain);
}

int main(int argc, char** argv) {
  assert(argc == 5);
  const auto valid = read(argv[1]);
  assert(verify(valid, "unit.nightscout.invalid") == 0);
  assert(verify(valid, "wrong.nightscout.invalid") == BR_ERR_X509_BAD_SERVER_NAME);
  assert(verify(read(argv[2]), "unit.nightscout.invalid") == BR_ERR_X509_EXPIRED);
  assert(verify(read(argv[3]), "unit.nightscout.invalid") == BR_ERR_X509_NOT_TRUSTED);
  assert(verify(read(argv[4]), "unit.nightscout.invalid") == 0);  // RSA trust anchor.
  auto corrupt = valid;
  corrupt.back() ^= 1;
  assert(verify(corrupt, "unit.nightscout.invalid") != 0);
  // Repeat verification: dynamic trust-anchor memory must remain usable after free callbacks.
  assert(verify(valid, "unit.nightscout.invalid") == 0);
  puts("Trusted chain accepted; wrong host, expired, untrusted and tampered certificates rejected");
}
