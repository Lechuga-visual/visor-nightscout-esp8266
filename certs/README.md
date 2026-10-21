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
time checks remain enabled in CA mode. A private CA can be explicitly supplied through
the device's authenticated configuration page; it replaces the public store
for that server instead of bypassing certificate verification.

Firmware 0.4.0 also supports an explicit server public-key pin supplied through
the authenticated UI. This separate mode uses BearSSL's known-key TLS
authenticator: the peer must prove possession of the pinned private key, without
certificate-chain, hostname or certificate-date validation. It never accepts an
unknown key automatically. `tools/export_server_key.py` exports a key only after
normal hostname and certificate validation on a trusted PC. Pins are bound to
the configured HTTPS origin and cleared on origin changes. A key rotation needs
an explicit pin update. No server key is embedded in the distributed firmware.

Only after a successful pinned TLS request does the device use a fresh HTTP
Date header to synchronize its clock. Glucose timestamps are never treated as
the current time, and unverified HTTP responses never set the clock.
