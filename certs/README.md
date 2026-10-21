# Public HTTPS trust store

The generated `src/generated/public_ca_data.h` embeds the public roots distributed
by **certifi 2026.10.19** (Mozilla CA program). `manifest.json` records the source
and hashes. No server-specific certificate, system-local CA or development
proxy certificate is imported. The certificate data is distributed under the
Mozilla Public License 2.0; see `LICENSE-certifi`.

`PublicCaStore` implements BearSSL's dynamic trust-anchor interface. It keeps
DER certificates and subject-name SHA-256 indexes in flash, and decodes only
the requested root into RAM. Normal firmware builds use the checked-in data;
no download or filesystem upload is required. Firmware updates refresh roots.

Maintainers can reproduce the bundle with:

```bash
python -m pip install certifi==2026.10.19 cryptography==50.0.2
python tools/generate_trust_store.py
```

The generator rejects unsupported public-key types, non-CA certificates and
ambiguous duplicate subject names. Standard certificate-chain, server-name and
time checks remain enabled. A private CA can be explicitly supplied through
the device's authenticated configuration page; it replaces the public store
for that server instead of bypassing certificate verification.
