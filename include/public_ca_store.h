#pragma once

#include <memory>
#include <stdint.h>

#ifdef ESP8266
#include <CertStoreBearSSL.h>
#else
#include <bearssl.h>
#endif

// Lazy trust anchors: public certificates live in flash; only the matching
// root's decoder/key is allocated in RAM during certificate verification.
class PublicCaStore
#ifdef ESP8266
    : public BearSSL::CertStoreBase
#endif
{
 public:
  void installCertStore(br_x509_minimal_context* context);
  static size_t rootCount();

 private:
  std::unique_ptr<br_x509_decoder_context> decoder_;
  br_x509_trust_anchor anchor_{};
  uint8_t matchedHash_[32]{};
  static const br_x509_trust_anchor* find(void* context, void* hashedDn, size_t length);
  static void release(void* context, const br_x509_trust_anchor* anchor);
};
