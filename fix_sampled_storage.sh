#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

FILE="vk_sampled_to_storage.c"

echo "=== Fixing vk_sampled_to_storage.c ==="

if [[ ! -f "$FILE" ]]; then
    echo "ERROR: $FILE not found"
    exit 1
fi

cp "$FILE" "${FILE}.before-copy-fix"
echo "Backup: ${FILE}.before-copy-fix"

#
# Remove ALL previous attempts at adding the copy-command loaders.
#
python3 - "$FILE" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
text = path.read_text()

# Remove loader blocks introduced by previous attempts.
patterns = [
    r'''
    \n\s*PFN_vkCmdCopyBufferToImage\s+cmdCopyBufferToImage\s*=
    .*?
    \n\s*if\s*\(!cmdCopyBufferToImage\s*\|\|\s*!cmdCopyImageToBuffer\)\s*\{
    .*?
    \n\s*\}
    ''',
]

for pattern in patterns:
    text = re.sub(pattern, '\n', text, flags=re.S | re.X)

# Also remove any duplicated individual declarations if a malformed
# previous patch left pieces behind.
text = re.sub(
    r'\n\s*PFN_vkCmdCopyBufferToImage\s+cmdCopyBufferToImage\s*=.*?'
    r'\n\s*"vkCmdCopyBufferToImage"\s*\);',
    '',
    text,
    flags=re.S
)

text = re.sub(
    r'\n\s*PFN_vkCmdCopyImageToBuffer\s+cmdCopyImageToBuffer\s*=.*?'
    r'\n\s*"vkCmdCopyImageToBuffer"\s*\);',
    '',
    text,
    flags=re.S
)

path.write_text(text)
PY

#
# Replace direct command calls.
#
sed -i \
    -e 's/\bvkCmdCopyBufferToImage(/cmdCopyBufferToImage(/g' \
    -e 's/\bvkCmdCopyImageToBuffer(/cmdCopyImageToBuffer(/g' \
    "$FILE"

#
# Find the successful vkCreateDevice block.
#
DEVICE_RESULT_LINE=$(grep -n 'vkCreateDevice:' "$FILE" | head -1 | cut -d: -f1)

if [[ -z "$DEVICE_RESULT_LINE" ]]; then
    echo "ERROR: Could not find vkCreateDevice result"
    cp "${FILE}.before-copy-fix" "$FILE"
    exit 1
fi

echo "vkCreateDevice result line: $DEVICE_RESULT_LINE"

#
# Find the following:
#
#     if (r != VK_SUCCESS)
#
#     ...
#
# and insert immediately after that error-return block.
#
INSERT_LINE=$(awk -v start="$DEVICE_RESULT_LINE" '
    NR >= start {
        if ($0 ~ /if \(r != VK_SUCCESS\)/) {
            found_if = NR
        }

        if (found_if && NR > found_if && $0 ~ /return [0-9]+;/) {
            print NR
            exit
        }
    }
' "$FILE")

if [[ -z "$INSERT_LINE" ]]; then
    echo "ERROR: Could not locate vkCreateDevice error return"
    cp "${FILE}.before-copy-fix" "$FILE"
    exit 1
fi

echo "Insertion point: line $INSERT_LINE"

TMP=$(mktemp)

awk -v line="$INSERT_LINE" '
{
    print
    if (NR == line) {
        print ""
        print "    PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage ="
        print "        (PFN_vkCmdCopyBufferToImage)getDeviceProcAddr("
        print "            device,"
        print "            \"vkCmdCopyBufferToImage\");"
        print ""
        print "    PFN_vkCmdCopyImageToBuffer cmdCopyImageToBuffer ="
        print "        (PFN_vkCmdCopyImageToBuffer)getDeviceProcAddr("
        print "            device,"
        print "            \"vkCmdCopyImageToBuffer\");"
        print ""
        print "    if (!cmdCopyBufferToImage || !cmdCopyImageToBuffer) {"
        print "        printf(\"Required copy commands missing\\n\");"
        print "        return 10;"
        print "    }"
    }
}
' "$FILE" > "$TMP"

mv "$TMP" "$FILE"

echo
echo "=== Checking resulting source ==="

grep -n -A8 -B3 \
    -E 'cmdCopyBufferToImage|cmdCopyImageToBuffer' \
    "$FILE"

echo
echo "=== Checking for accidental direct Vulkan calls ==="

if grep -nE '[^A-Za-z0-9_]vkCmdCopy(BufferToImage|ImageToBuffer)\(' "$FILE"; then
    echo "ERROR: Direct copy-command call remains"
    exit 1
fi

echo "No direct copy-command calls remain."

echo
echo "=== Compiling ==="

clang \
    -O2 \
    -Wall \
    -Wextra \
    -std=c11 \
    "$FILE" \
    -ldl \
    -o vk_sampled_to_storage

echo
echo "=========================================="
echo "COMPILE: PASS"
echo "=========================================="
echo
echo "Executable:"
echo "  $PWD/vk_sampled_to_storage"
echo
echo "Backup:"
echo "  $PWD/${FILE}.before-copy-fix"
echo
echo "Run:"
echo "  ./vk_sampled_to_storage 2>&1 | tee xclipse940-sampled-storage.txt"
