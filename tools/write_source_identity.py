# SPDX-License-Identifier: GPL-3.0-only
"""Pin broker/controller source revision before compiling either binary."""
from pathlib import Path
import hashlib
root = Path(__file__).resolve().parent.parent
files = sorted((root / 'src').glob('*'))
digest = hashlib.sha256()
for path in files:
    if path.is_file():
        name = path.name.encode('utf-8')
        data = path.read_bytes()
        digest.update(len(name).to_bytes(8, 'little') + name)
        digest.update(len(data).to_bytes(8, 'little') + data)
out = root / 'out'
out.mkdir(exist_ok=True)
values = ','.join(f'0x{x:02x}' for x in digest.digest())
(out / 'broker_build_identity.hpp').write_text(
    '// Generated from this candidate source, before DLL/controller build.\n'
    '#pragma once\ninline constexpr unsigned char BrokerRevision[32]={'
    + values + '};\n', encoding='ascii')
print('Broker source revision SHA256 ' + digest.hexdigest())
