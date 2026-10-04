# ModLoader

Requires CMake, Ninja, Clang, Python 3, and Rust. On Windows, install Visual Studio C++ build tools and the Windows SDK.

## SDK

To build the SDK:

```sh
git clone --recurse-submodules https://github.com/ModLoader64/ModLoader.git
cd ModLoader
mkdir build-sdk
cd build-sdk
cmake -G Ninja ../sdk
cmake --build .
```

See `build-sdk/dist`

## Client

To build the client:

```sh
mkdir build-client
cd build-client
cmake -G Ninja .. "-DMODLOADER_SDK=/path/to/sdk"
cmake --build .
```

See `build-client/dist`

