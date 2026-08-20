#!/system/bin/sh
set -eu

FILE="${1:-src/layer/layer_entry.cpp}"

python3 - "$FILE" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()

include = '#include "layer_lsfg_compat.h"'
if include not in text:
    marker = '#include "layer_entry.h"'
    if marker in text:
        text = text.replace(marker, marker + '\\n' + include, 1)
    else:
        raise SystemExit("could not find a safe include insertion point in layer_entry.cpp")

include = '#include "layer_lsfg_compat.h"'
if include not in text:
    marker = '#include "layer_entry.h"'
    if marker in text:
        text = text.replace(marker, marker + '\n' + include, 1)
    else:
        raise SystemExit("could not find a safe include insertion point in layer_entry.cpp")
old = '''    DescriptorBufferCreateSupport descriptor_buffer_support =
        query_descriptor_buffer_create_support(physicalDevice, instance, instance_dispatch);
    PhysicalRuntime physical_runtime{};
'''
new = '''    const bool lsfg_process_active = exynos_lsfg_process_active();

    DescriptorBufferCreateSupport descriptor_buffer_support{};
    if (!lsfg_process_active) {
        descriptor_buffer_support =
            query_descriptor_buffer_create_support(physicalDevice, instance, instance_dispatch);
    }
    PhysicalRuntime physical_runtime{};
'''
if old not in text:
    raise SystemExit("expected descriptor-buffer query block was not found")
text = text.replace(old, new, 1)
old = '''    const bool lsfg_process_active = exynos_lsfg_process_active();

    bool should_inject_descriptor_buffer =
'''
new = '''    bool should_inject_descriptor_buffer =
'''
if old not in text:
    raise SystemExit("expected existing LSFG declaration was not found")
text = text.replace(old, new, 1)
path.write_text(text)
PY
