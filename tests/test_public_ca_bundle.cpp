#include <bearssl.h>
#include "../src/generated/public_ca_data.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void appendName(void* context, const void* data, size_t length) {
  br_sha256_update(static_cast<br_sha256_context*>(context), data, length);
}

int main() {
  for (size_t i = 0; i < public_ca_data::count; ++i) {
    const auto& entry = public_ca_data::index[i];
    assert(entry.offset + entry.length <= sizeof(public_ca_data::certificates));
    br_sha256_context hash;
    br_sha256_init(&hash);
    br_x509_decoder_context decoder;
    br_x509_decoder_init(&decoder, appendName, &hash, nullptr, nullptr);
    br_x509_decoder_push(&decoder, public_ca_data::certificates + entry.offset, entry.length);
    assert(br_x509_decoder_last_error(&decoder) == 0);
    assert(br_x509_decoder_isCA(&decoder));
    assert(br_x509_decoder_get_pkey(&decoder));
    uint8_t digest[32];
    br_sha256_out(&hash, digest);
    assert(memcmp(digest, entry.subjectHash, sizeof(digest)) == 0);
  }
  printf("%zu public roots decoded and subject hashes verified by BearSSL\n", public_ca_data::count);
}
