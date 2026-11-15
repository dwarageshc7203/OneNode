# CMake changes for the packaging (apply to the top-level CMakeLists.txt)

1. Directly **above** `find_package(OpenSSL 3.0 REQUIRED)`:

```cmake
# The release workflow for macOS links OpenSSL into the program (the dylibs for Qt's TLS plugin are copied in separately)
option(ONENODE_STATIC_OPENSSL "Link OpenSSL statically instead of using its shared library" OFF)
if(ONENODE_STATIC_OPENSSL)
    set(OPENSSL_USE_STATIC_LIBS ON)
endif()
```

2. In `set_target_properties(appOneNode PROPERTIES ...)`, add one line (the name macOS shows):

```cmake
    MACOSX_BUNDLE_BUNDLE_NAME "OneNode"
```
