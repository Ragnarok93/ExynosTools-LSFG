#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VULKAN_TAG="${VULKAN_TAG:-v1.4.341}"

clone_pinned() {
  local url="$1"
  local path="$2"
  if [[ -e "$path" ]]; then
    echo "Refusing to replace existing dependency path: $path" >&2
    echo "Remove it first if you want a clean pinned checkout." >&2
    exit 1
  fi
  git clone --depth 1 --branch "$VULKAN_TAG" "$url" "$path"
}

mkdir -p external
clone_pinned https://github.com/KhronosGroup/Vulkan-Headers.git external/Vulkan-Headers
clone_pinned https://github.com/KhronosGroup/Vulkan-Utility-Libraries.git external/Vulkan-Utility-Libraries

test -f external/VulkanMemoryAllocator/include/vk_mem_alloc.h || {
  echo "Missing tracked VulkanMemoryAllocator header." >&2
  exit 1
}

echo "Pinned Vulkan dependencies ready at $VULKAN_TAG"
