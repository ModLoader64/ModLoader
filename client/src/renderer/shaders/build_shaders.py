#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SHADERS = [
    ("ui_vertex", "ui.hlsl", "Vertex_Main", "vs"),
    ("ui_pixel", "ui.hlsl", "Pixel_Main", "ps"),
    ("image_vertex", "image.hlsl", "Vertex_Main", "vs"),
    ("image_pixel", "image.hlsl", "Pixel_Main", "ps"),
]


def find(name, candidates):
    found = shutil.which(name)
    if found:
        return found
    for candidate in candidates:
        if candidate.exists():
            return str(candidate)
    sys.exit(f"cannot find {name}; pass --{name}")


def vulkan_sdk_tool(name):
    sdk = os.environ.get("VULKAN_SDK")
    return [Path(sdk) / "Bin" / f"{name}.exe", Path(sdk) / "bin" / name] if sdk else []


def compile_shader(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"{' '.join(command)}\n{result.stdout}{result.stderr}")


def main():
    parser = argparse.ArgumentParser(description="Compiles the renderer's shaders into the .spv files")
    parser.add_argument("--dxc")
    arguments = parser.parse_args()
    dxc = arguments.dxc or find("dxc", vulkan_sdk_tool("dxc"))

    for name, source, entry, stage in SHADERS:
        spirv = HERE / f"{name}.spv"
        compile_shader([dxc, "-nologo", "-O3", "-spirv", "-fspv-target-env=vulkan1.1", "-T", f"{stage}_6_0", "-E", entry, "-Fo", str(spirv), str(HERE / source)])
        print(spirv)


if __name__ == "__main__":
    main()
