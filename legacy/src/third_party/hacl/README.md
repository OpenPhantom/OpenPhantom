# HACL*, the subset the relay transport uses

**Source:** https://github.com/hacl-star/hacl-star, commit
`504c2987452f87fe44bce9b9f12e19d6e051761f`, directory `dist/msvc-compatible` and the KaRaMeL headers
under `dist/karamel`. Every file here is a byte-for-byte copy; nothing was edited. `LICENSE` is
`dist/LICENSE.txt` of the same commit (MIT). The KaRaMeL headers carry their own notice, Apache 2.0
and MIT.

**What it is for.** The relay protocol's handshake and record layer (`mp_relay_crypto.c`,
`mp_relay_noise.c`): X25519 (`Hacl_Curve25519_51`), ChaCha20-Poly1305 (`Hacl_AEAD_Chacha20Poly1305`
with `Hacl_Chacha20` and `Hacl_MAC_Poly1305`), SHA-256 (`Hacl_Hash_SHA2`). HMAC-SHA-256 and the
two-output HKDF of the Noise framework are a few lines in `mp_relay_crypto.c` over this SHA-256,
checked against RFC 4231 and RFC 5869; `Hacl_HMAC.c` would link every hash HACL* has.

**Why these builds.** `Hacl_Curve25519_51` is the portable 51-bit limb implementation with its
128-bit arithmetic from `FStar_UInt128_Verified.h`, which is what a 32-bit build needs; no SIMD file
is built.

**Build.** `src/third_party/CMakeLists.txt`, the static library `mp_hacl`, `/W3 /GS /Gy`.

**Updating.** Copy the same file list from a newer commit, run the `mp_relay_crypto` test, and
change the commit above in the same change.
