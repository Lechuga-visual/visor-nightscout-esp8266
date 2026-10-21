#include "public_ca_store.h"

#include <cstring>
#include <new>

#ifndef PUBLIC_CA_DATA_HEADER
#define PUBLIC_CA_DATA_HEADER "generated/public_ca_data.h"
#endif
#include PUBLIC_CA_DATA_HEADER

#ifndef ESP8266
#define memcpy_P memcpy
#endif

void PublicCaStore::installCertStore(br_x509_minimal_context* context) {
  br_x509_minimal_set_dynamic(context, this, find, release);
}

size_t PublicCaStore::rootCount() { return public_ca_data::count; }

const br_x509_trust_anchor* PublicCaStore::find(void* context, void* hashedDn, size_t length) {
  auto* store = static_cast<PublicCaStore*>(context);
  if (!store || !hashedDn || length != sizeof(store->matchedHash_)) return nullptr;
  for (size_t i = 0; i < public_ca_data::count; ++i) {
    public_ca_data::Entry entry;
    memcpy_P(&entry, &public_ca_data::index[i], sizeof(entry));
    if (memcmp(entry.subjectHash, hashedDn, length) != 0) continue;
    std::unique_ptr<uint8_t[]> der(new (std::nothrow) uint8_t[entry.length]);
    store->decoder_.reset(new (std::nothrow) br_x509_decoder_context);
    if (!der || !store->decoder_) { store->decoder_.reset(); return nullptr; }
    memcpy_P(der.get(), public_ca_data::certificates + entry.offset, entry.length);
    br_x509_decoder_init(store->decoder_.get(), nullptr, nullptr, nullptr, nullptr);
    br_x509_decoder_push(store->decoder_.get(), der.get(), entry.length);
    const br_x509_pkey* key = br_x509_decoder_get_pkey(store->decoder_.get());
    if (!key || br_x509_decoder_last_error(store->decoder_.get()) ||
        !br_x509_decoder_isCA(store->decoder_.get()) ||
        (key->key_type != BR_KEYTYPE_RSA && key->key_type != BR_KEYTYPE_EC)) {
      store->decoder_.reset();
      return nullptr;
    }
    memcpy(store->matchedHash_, entry.subjectHash, sizeof(store->matchedHash_));
    store->anchor_.dn.data = store->matchedHash_;
    store->anchor_.dn.len = sizeof(store->matchedHash_);
    store->anchor_.flags = BR_X509_TA_CA;
    store->anchor_.pkey = *key;  // Key bytes belong to decoder_ until release().
    return &store->anchor_;
  }
  return nullptr;
}

void PublicCaStore::release(void* context, const br_x509_trust_anchor*) {
  auto* store = static_cast<PublicCaStore*>(context);
  if (store) store->decoder_.reset();
}
