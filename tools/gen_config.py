"""Writes config/game.toml for Genma Onimusha (XDK 3911).

The SDK ships byte signatures only for XDK 5849, so the symbols come from
XbSymbolDatabase (config/xsd_3911.txt, `XbSymbolDatabaseCLI default.xbe -e`)
plus the addresses below that were read from the disassembly by hand.
Every name the runtime binds to is written, 0 where this image has none.

usage: python tools/gen_config.py [--sdk ../XboxXBERecomp]
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SDK = Path(sys.argv[sys.argv.index('--sdk') + 1]) if '--sdk' in sys.argv else ROOT / 'sdk'

# XbSymbolDatabase name -> runtime name, where they differ
ALIAS = {
    'D3D_g_pDevice': 'D3D_pDevice',
    'D3D_g_RenderState': 'D3D_RenderState',
    'D3D_g_DeferredTextureState': 'D3D_DeferredTextureState',
    'D3D_g_Stream': 'D3D_Streams',
    'g_DeviceType_Gamepad': 'XPP_GamepadType',
    'g_DeviceType_MU': 'XPP_MemoryUnitType',
    '_tls_index': 'XAPI_TlsIndex',
    'CDevice_MakeSpace': 'D3DDevice_MakeSpace',  # thiscall, no stack arguments: the stdcall host version fits
}

# read from the disassembly, see sdk/xdk/3911/layout.toml for the evidence
MANUAL_DATA = {
    'D3D_Device': 0x191490,                   # Direct3D_CreateDevice 0x1862DB
    'D3D_DirtyFlags': 0x191498,               # the device's +8, D3DDevice_SetTexture 0x1856A2
    'D3D_IndexData': 0x190E0C,                # D3DDevice_SetIndices 0x185829
    'D3D_SimpleRenderStateEncode': 0x1D0C80,  # D3DDevice_SetRenderStateNotInline 0x182C9A
    'D3D_FvfShader': 0x191318,                # D3DDevice_SetVertexShader 0x184AB2
}
MANUAL_SYMBOLS = {
    'D3D_InitDefaultState': 0x18B040,  # CDevice init 0x18BA3D: identity transforms, default render and stage states
    'D3D_InternalAddRef': 0x185CE0,    # D3DDevice_SetIndices 0x185821, a surface also adds to its parent
}

# entries the analysis misses, by hand (data-table dispatchers are handled by the recompiler now)
EXTRA_FUNCTIONS = [
]

# host functions run before a guest instruction: (address, function); see src/game/difftest.cpp
HOOKS = [
    (0xEEC80, 'DiffBefore'),  # skeleton build: entry
    (0xEEDB8, 'DiffAfter'),   # skeleton build: ret
]

DATA_NAMES = [
    'D3D_DeferredTextureState', 'D3D_Device', 'D3D_DirtyFlags', 'D3D_IndexData', 'D3D_RenderState',
    'D3D_SimpleRenderStateEncode', 'D3D_Streams', 'D3D_pDevice', 'DSound_StreamVtable', 'DSound_StreamVtable2',
    'XPP_GamepadType', 'XPP_MemoryUnitType', 'D3D_FvfShader', 'XAPI_TlsIndex',
]
# entry points of XDK 3911 the runtime replaces beyond the 5849 set
EXTRA_SYMBOLS = [
    'D3DDevice_Present', 'D3DDevice_GetBackBuffer', 'D3DDevice_GetRenderTarget', 'D3DDevice_GetDepthStencilSurface',
    'D3DDevice_SetVertexShaderConstant', 'D3DDevice_SetRenderState_MultiSampleType', 'D3DDevice_SetRenderState_ZBias',
    'ConvertThreadToFiber', 'CreateFiber', 'DeleteFiber', 'SwitchToFiber', 'XInputGetCapabilities',
    'IDirectSoundBuffer_SetMixBinVolumes_12', 'IDirectSoundBuffer_SetMode', 'IDirectSound_SetAllParameters',
]


def needed_symbols():
    text = (SDK / 'xdk' / '5849' / 'signatures.toml').read_text()
    names, section = [], None
    for line in text.splitlines():
        if line.startswith('['):
            section = line.strip('[] ')
            continue
        m = re.match(r'([A-Za-z_0-9]+) = ', line)
        if m and section in ('symbols', 'anchors'):
            names.append(m.group(1))
    return [n for n in names if n not in DATA_NAMES and not n.endswith('_ref')] + EXTRA_SYMBOLS


def scanned():
    found = {}
    for line in (ROOT / 'config' / 'xsd_3911.txt').read_text().splitlines():
        m = re.match(r'[A-Z0-9]+__(?:FUN|VAR)__(?:[a-z]+__)?([A-Za-z_0-9]+)(?:\(.*\))? = (0x[0-9a-fA-F]+)', line)
        if m:
            name = ALIAS.get(m.group(1), m.group(1))
            found.setdefault(name, int(m.group(2), 16))
    return found


def table(m):
    w = max(len(k) for k in m)
    return ''.join(f'{k:<{w}} = 0x{v:08X}\n' for k, v in sorted(m.items(), key=lambda kv: kv[0].lower()))


def main():
    found = scanned()
    found.update(MANUAL_SYMBOLS)
    found.update(MANUAL_DATA)
    symbols = {n: found.get(n, 0) for n in needed_symbols()}
    data = {n: found.get(n, 0) for n in DATA_NAMES}
    out = ROOT / 'config' / 'game.toml'
    head = out.read_text().split('# extra entry points')[0] if out.exists() else ''
    if '[xbe]' not in head:
        sys.exit('config/game.toml has no [xbe] table, run xbe_symbols first')
    s = head + '# extra entry points\n[functions]\n'
    s += '\n# functions the host replaces or calls by name, 0 where the image has none\n[symbols]\n' + table(symbols)
    s += '\n[data]\n' + table(data)
    s += '\n' + ''.join('[[function]]\nat = 0x%06X\n' % f for f in EXTRA_FUNCTIONS)
    s += ''.join('\n[[hook]]\nat = 0x%06X\ncall = "%s"\n' % h for h in HOOKS)
    out.write_text(s, newline='\n')
    have = sum(1 for v in symbols.values() if v)
    print(f'{have} of {len(symbols)} symbols, {sum(1 for v in data.values() if v)} of {len(data)} data')
    print('missing:', ' '.join(k for k, v in sorted(symbols.items()) if not v))


if __name__ == '__main__':
    main()
