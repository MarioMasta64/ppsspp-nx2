#!/usr/bin/env python3
"""Link a second core, then rename every core-owned global symbol as one unit."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ld', required=True)
parser.add_argument('--nm', required=True)
parser.add_argument('--objcopy', required=True)
parser.add_argument('--readelf', required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--object', action='append', required=True)
parser.add_argument('archives', nargs='+')
args = parser.parse_args()
args.output.parent.mkdir(parents=True, exist_ok=True)
plain = args.output.with_suffix('.unprefixed.o')
subprocess.run([args.ld, '-r', '-o', str(plain), *args.object,
                '--start-group', *args.archives, '--end-group'], check=True)


def comdat_signatures(obj):
    groups = subprocess.check_output([args.readelf, '--section-groups', str(obj)], text=True)
    return re.findall(r'^COMDAT group section .* \[([^\]]+)\] contains', groups, re.MULTILINE)


output = subprocess.check_output([args.nm, '-g', '--defined-only', str(plain)], text=True)
names = {line.split()[-1] for line in output.splitlines() if len(line.split()) >= 3}
exports = {name for name in names if name.startswith('ppsspp_guest_')}
assert len(exports) == 8, exports
# GCC can use a local C5/D5 symbol as the group signature for global
# constructor/destructor aliases. If it survives unchanged, the final link
# can discard the host's group even though the guest's aliases were renamed.
signatures = comdat_signatures(plain)
assert signatures, 'Expected C++ COMDAT groups in the guest'
private_names = (names | set(signatures)) - exports
assert not set(signatures) & exports, 'Public guest ABI must not be coalesced'
renames = {name: 'ppsspp_guest_private_' + name for name in private_names}
for required in ('g_Config', '_ZN6Memory4baseE', 'coreState'):
    assert required in renames, f'Missing isolation boundary: {required}'
mapfile = args.output.with_suffix('.symbols.txt')
mapfile.write_text(''.join(f'{name} {renames[name]}\n' for name in sorted(renames)))
subprocess.run([args.objcopy, '--redefine-syms=' + str(mapfile), str(plain), str(args.output)], check=True)
after = subprocess.check_output([args.nm, '-g', '--defined-only', str(args.output)], text=True)
actual = {line.split()[-1] for line in after.splitlines() if len(line.split()) >= 3}
assert actual == exports | {renames[name] for name in names - exports}, 'Guest symbol isolation audit failed'
assert Counter(comdat_signatures(args.output)) == Counter(renames[name] for name in signatures), (
    'Guest COMDAT group isolation audit failed'
)
args.output.with_suffix('.isolation.json').write_text(json.dumps({
    'exports': sorted(exports), 'renamed_symbols': len(names - exports),
    'renamed_comdat_groups': len(signatures),
    'private_prefix': 'ppsspp_guest_private_', 'archives': args.archives,
}, indent=2) + '\n')
print(f'Isolated second PPSSPP core: {len(names - exports)} private global symbols, '
      f'{len(signatures)} private COMDAT groups, {len(exports)} ABI exports')
