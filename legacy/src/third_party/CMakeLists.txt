# Code the multiplayer's relay transport stands on that is not this project's own.
#
# Both libraries are built with the compiler's defaults for somebody else's code, /W3 with the
# buffer security check on, and not with this tree's /W4 /WX. A warning in published,
# verified source is not ours to fix, and failing the build over one would only invite an edit
# to code that has to stay as it was released. Only multiplayer.dll links them.
#
# The tree's options arrive here from the root directory and are replaced for everything below.
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS /W3 /GS)

# HACL*: X25519, ChaCha20-Poly1305 and SHA-256, formally verified, the portable build with no
# vector code. Five sources and the headers they include, unchanged; hacl/README.md names the
# commit.
add_library(mp_hacl STATIC
    hacl/Hacl_Curve25519_51.c
    hacl/Hacl_Chacha20.c
    hacl/Hacl_MAC_Poly1305.c
    hacl/Hacl_AEAD_Chacha20Poly1305.c
    hacl/Hacl_Hash_SHA2.c
)
target_include_directories(mp_hacl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/hacl
    ${CMAKE_CURRENT_SOURCE_DIR}/hacl/karamel/include
    ${CMAKE_CURRENT_SOURCE_DIR}/hacl/karamel/krmllib/dist/minimal
)
target_compile_options(mp_hacl PRIVATE /Gy)

# Mbed TLS 4.1.1 with TF-PSA-Crypto: the TLS 1.3 client that fetches the relay's key document.
# It is too large to carry in this tree, so the configure step downloads the release archive and
# refuses it unless its SHA-256 is the one below, the value the release publishes. The two
# headers in mbedtls/ replace its default configuration with a TLS 1.3 client and no more.
include(FetchContent)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(TF_PSA_CRYPTO_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
set(MBEDTLS_CONFIG_FILE ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/mp_relay_tls_config.h
    CACHE FILEPATH "" FORCE)
set(TF_PSA_CRYPTO_CONFIG_FILE ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/mp_relay_psa_config.h
    CACHE FILEPATH "" FORCE)
# The extracted files keep the times the archive records. Saying so is also what stops CMake
# warning about policy CMP0135 on every configure.
FetchContent_Declare(mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-4.1.1/mbedtls-4.1.1.tar.bz2
    URL_HASH SHA256=3359a349e23db3d5536fcee032ae7b2ecbfc08972fab643089b5cbf2a375c98c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(mbedtls)
