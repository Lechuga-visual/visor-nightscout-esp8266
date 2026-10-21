#pragma once

#ifdef ESP8266
#include <bearssl/bearssl.h>
#else
#include <bearssl.h>
#endif
#include <stdlib.h>
#include <string.h>

// ESP8266 core 3.1.2's public-key decoder consumes the uncompressed-point 0x04
// marker in SPKI EC keys. Restore it before passing the point to TLS. Newer
// decoders that retain the marker pass through unchanged. The input q MUST be
// malloc-owned, as it is in BearSSL::PublicKey; this function may replace it.
inline bool normalizeOwnedPinnedEcPoint(br_ec_public_key& key) {
  const size_t expected = key.curve == BR_EC_secp256r1 ? 65 :
                          key.curve == BR_EC_secp384r1 ? 97 :
                          key.curve == BR_EC_secp521r1 ? 133 : 0;
  if (!expected || !key.q) return false;
  if (key.qlen == expected) return key.q[0] == 0x04;
  if (key.qlen != expected - 1) return false;
  auto* point = static_cast<unsigned char*>(malloc(expected));
  if (!point) return false;
  point[0] = 0x04;
  memcpy(point + 1, key.q, key.qlen);
  free(key.q);
  key.q = point;
  key.qlen = expected;
  return true;
}
