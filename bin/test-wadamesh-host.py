#!/usr/bin/env python3
"""Compile the real backend and PacketClient/PacketServer on macOS/Linux with sanitizers."""
from pathlib import Path
import subprocess, os, sys
root=Path(__file__).resolve().parents[1]
source=root/'test/wadamesh_host'
build=root/'work/wadamesh-host'
build.mkdir(parents=True,exist_ok=True)
deps=root/'.pio/libdeps/t-deck-tft'
np=deps/'Nanopb'
if not (np/'pb.h').exists():raise SystemExit('Run pio run -e t-deck-tft to install pinned dependencies first.')
includes=[source/'shims',root/'lib/WadaApp/port',root/'lib/WadaApp/backend',root/'lib/WadaApp/vendor',root/'lib/WadaPacketComms/include',root/'src/mesh/generated',root/'src',np,deps/'CayenneLPP/src',deps/'Crypto']
sanitizers='undefined' if sys.platform=='darwin' else 'address,undefined'
flags=['-fsanitize='+sanitizers,'-fno-omit-frame-pointer','-g','-O1','-DPB_ENABLE_MALLOC=1','-DPB_VALIDATE_UTF8=1']+[f'-I{p}' for p in includes]
objects=[]
cs=[np/'pb_common.c',np/'pb_decode.c',np/'pb_encode.c']+[p for p in (root/'src/mesh/generated/meshtastic').glob('*.pb.cpp') if not p.name.startswith('deviceonly')]
cpp=[deps/'Crypto/SHA256.cpp',deps/'Crypto/Hash.cpp',deps/'Crypto/Crypto.cpp',root/'src/mesh/Throttle.cpp',root/'lib/WadaApp/port/WadaMeshtasticBackend.cpp',root/'lib/WadaApp/port/WadaTerminal.cpp',root/'lib/WadaApp/port/WadaNativeConfig.cpp',root/'lib/WadaApp/port/WadaTelemetry.cpp',root/'lib/WadaApp/port/WadaNodeState.cpp',root/'lib/WadaApp/port/WadaNodes.cpp',root/'lib/WadaApp/port/WadaStatus.cpp',root/'lib/WadaApp/port/WadaWifi.cpp',source/'test_main.cpp',source/'test_telemetry.cpp',source/'test_regions.cpp',source/'test_nodes.cpp',source/'test_status.cpp',source/'test_wifi_bridge.cpp',*list((deps/'CayenneLPP/src').glob('*.cpp'))]+list((root/'lib/WadaPacketComms/src').glob('*.cpp'))
for i,p in enumerate(cs+cpp):
    obj=build/f'{i}.o';objects.append(str(obj))
    cxx=p.suffix=='.cpp'
    subprocess.run(['clang++' if cxx else 'clang','-std=c++17' if cxx else '-std=c11',*flags,'-c',str(p),'-o',str(obj)],check=True)
exe=build/'test-wadamesh'
subprocess.run(['clang++',*flags,*objects,'-o',str(exe)],check=True)
runtime_env = dict(os.environ)
runtime_env['UBSAN_OPTIONS'] = runtime_env.get('UBSAN_OPTIONS', '') + ':halt_on_error=1'
subprocess.run([str(exe)],check=True,timeout=30,env=runtime_env)
print("Sanitizers:",sanitizers)

# Compile the production native handoff and action bodies with a deterministic NodeDB fixture.
def extract_function(path, signature):
    text=path.read_text();start=text.index(signature);body=text.index('{',start)
    return text[start:text.index('\n}',body)+2]+'\n'
(build/'native_nodes_core.inc').write_text(
    extract_function(root/'src/mesh/api/PacketAPI.cpp','bool PacketAPI::readPrivateReceiveQueue(')+
    extract_function(root/'src/mesh/api/PacketAPI.cpp','void PacketAPI::refreshNodeFacts(')+
    extract_function(root/'src/wadamesh/WadaFirmware.cpp','bool wadaProcessNodeActions('))
core_exe=build/'test-native-node-core'
subprocess.run(['clang++','-std=c++17',*flags,f'-I{build}',str(source/'test_nodes_core.cpp'),*objects[:len(cs)],'-o',str(core_exe)],check=True)
subprocess.run([str(core_exe)],check=True,timeout=30,env=runtime_env)

# Pin committed broadcasts and replay ordering using production native bodies.
(build/'native_status_core.inc').write_text(
    extract_function(root/'src/modules/StatusMessageModule.cpp','StatusMessageModule::StatusMessageModule(')+
    extract_function(root/'src/modules/StatusMessageModule.cpp','void StatusMessageModule::onConfigChanged(')+
    extract_function(root/'src/modules/StatusMessageModule.cpp','int32_t StatusMessageModule::runOnce(')+
    extract_function(root/'src/mesh/api/PacketAPI.cpp','void PacketAPI::refreshStatus(')+
    extract_function(root/'src/mesh/MeshService.cpp','void MeshService::reloadConfig(').replace('void MeshService::reloadConfig(', 'void NativeService::reloadConfig(')+
    extract_function(root/'src/modules/AdminModule.cpp','void AdminModule::saveChanges('))
status_exe=build/'test-native-status-core'
subprocess.run(['clang++','-std=c++17',*flags,f'-I{build}',str(source/'test_status_core.cpp'),*objects[:len(cs)],'-o',str(status_exe)],check=True)
subprocess.run([str(status_exe)],check=True,timeout=30,env=runtime_env)
