#!/bin/bash

sdk_flag=false
client_flag=false
clean_flag=false

# Parse options
while getopts ":scx" opt; do
    case "$opt" in
        s) sdk_flag=true ;;
        c) client_flag=true ;;
        x) clean_flag=true ;;
        *)
            echo "Usage: $0 [-s] [-c] [-x]"
            echo ""
            echo "Options:"
            echo "  -s    Build SDK"
            echo "  -c    Build Client"
            echo "  -x    Clean build directories before building"
            echo "  -h    Show this help message"
            exit 0
            ;;
            exit 1
            ;;
    esac
done

if [[ "$sdk_flag" == false && "$client_flag" == false ]]; then
    sdk_flag=true
    client_flag=true
fi

if [[ "$clean_flag" == true ]]; then
    echo "Cleaning build directories..."

    if [[ "$sdk_flag" == true ]]; then
        rm -rf build-sdk
    fi

    if [[ "$client_flag" == true ]]; then
        rm -rf build-client
    fi
fi

# Build SDK
if [[ "$sdk_flag" == true ]]; then
    echo "Building SDK..."

    mkdir -p build-sdk
    cmake -S sdk -B build-sdk -G Ninja
    cmake --build build-sdk
fi

# Build Client
if [[ "$client_flag" == true ]]; then
    echo "Building Client..."

    mkdir -p build-client
    cmake -S . -B build-client -G Ninja \
        "-DMODLOADER_SDK=../build/dist"
    cmake --build build-client
fi

echo "Build completed successfully."