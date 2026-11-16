#!/usr/bin/env python3
"""Run WADAMESH's retained map, keyboard, name-storage and Lua range host tests."""
from pathlib import Path
import os,subprocess,sys
os.environ["UBSAN_OPTIONS"] = os.environ.get("UBSAN_OPTIONS", "") + ":halt_on_error=1"
root=Path(__file__).resolve().parents[1]
test=root/'test/wadamesh_host'
ui=root/'lib/WadaApp/vendor'
build=root/'work/wadamesh-ui';build.mkdir(parents=True,exist_ok=True)
sanitize='undefined' if sys.platform=='darwin' else 'address,undefined'
flags=['-O1','-g','-fsanitize='+sanitize,'-fno-omit-frame-pointer','-DMINIZ_NO_ARCHIVE_APIS','-DMINIZ_NO_ZLIB_APIS','-DMINIZ_NO_DEFLATE_APIS']
inc=[f'-I{ui}',f'-I{ui}/ui-touch',f'-I{test}/miniz']
obj=build/'tinfl.o'
subprocess.run(['clang','-std=c11',*flags,*inc,'-c',str(test/'miniz/miniz_tinfl.c'),'-o',str(obj)],check=True)
fixtures=build/'fixtures'
subprocess.run([sys.executable,str(test/'pmtiles/make_fixtures.py'),str(fixtures)],check=True)
for name in ['pmtiles','tdeck_keyboard_state','sender_ext_field','lua_random_range','native_qr_layout']:
    inputs=[str(test/f'test_{name}.cpp')]
    if name=='pmtiles':inputs += [str(ui/'ui-touch/PmTiles.cpp'),str(obj)]
    exe=build/name
    subprocess.run(['clang++','-std=c++17',*flags,*inc,*inputs,'-o',str(exe)],check=True)
    subprocess.run([str(exe)]+([str(fixtures)] if name=='pmtiles' else []),check=True,timeout=30)
    print('PASS:',name,flush=True)
exe=build/'test-storage-partition'
subprocess.run(['clang++','-std=c++17',*flags,f'-I{test}/shims',f'-I{root}/lib/WadaApp/port',str(test/'test_storage_partition.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)

# Compile the actual production policy and codec bodies against a small snapshot/storage fixture.
source=(ui/'ui-touch/UITask.cpp').read_text()
header=(ui/'ui-touch/UITask.h').read_text()
def function(signature, content=source):
    start=0
    while True:
        start=content.index(signature,start)
        body=content.index('{',start)
        if ';' not in content[start:body]:
            return content[start:content.index('\n}',body)+2]+'\n'
        start+=len(signature)
def structure(text,signature,end):
    start=text.index(signature)
    return text[start:text.index(end,start)+len(end)]+'\n'
(build/'native_channel_records.inc').write_text(
    structure(header,'  struct UIMessage {','\n  };')+
    structure(header,'  struct UIThread {','\n  };'))
(build/'native_channel_types.inc').write_text(
    'constexpr int k_ui_disk_sender_len = 25;\n'+
    structure(source,'struct __attribute__((packed)) UiHistoryMsg {','\n};')+
    structure(source,'struct __attribute__((packed)) UiSegMsg {','\n};'))
(build/'native_channel_policy.inc').write_text('\n'.join(function(name) for name in [
    'static bool wadaBytesPresent(', 'bool UITask::ensureNativeChannelConversation(',
    'void UITask::repairNativeChannelHistory(', 'bool UITask::channelThreadAvailable(',
    'void UITask::refreshNativeChannels(', 'uint32_t UITask::newestThreadMessageTs(',
    'int UITask::findOrCreateThread(', 'void UITask::sortThreadsByRecent(', 'bool UITask::removeThread(',
    'static bool readHistoryRec(', 'static void uiSegMarshal(', 'static bool uiSegReadRec(']))
exe=build/'native-channels'
subprocess.run(['clang++','-std=c++17',*flags,*inc,f'-I{build}',f'-I{root}/lib/WadaApp/port',
                str(test/'test_native_channels.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)

# Exercise the actual modal callbacks: typed requests, Host gating, and late-reply isolation.
(build/'native_telemetry_ui.inc').write_text('\n'.join(function(name) for name in [
    'static void wadaTelemetryRefresh(', 'static void wadaTelemetryRequestCb(',
    'static void wadaTelemetryNodeNote(', 'static void wadaTelemetryOpenLog(',
    'static void wadaTelemetryPickCb(', 'static void wadaTelemetryPicker(',
    'void UITask::onMeshtasticTelemetry(', 'void UITask::onMeshtasticTelemetryStatus(',
    'static bool wadaDeviceWidgetReading(']))
exe=build/'native-telemetry-ui'
subprocess.run(['clang++','-std=c++17',*flags,f'-I{build}',f'-I{root}/lib/WadaApp/port',
                f'-I{root}/src',f'-I{root}/src/mesh/generated',f'-I{root}/.pio/libdeps/t-deck-tft/Nanopb',
                str(test/'test_native_telemetry_ui.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)

# The settings form must map filtered dropdown positions back to real modem enums.
settings=(root/'lib/WadaApp/port/WadaSettings.inc').read_text()
(build/'native_preset_ui.inc').write_text('\n'.join(function(name,settings) for name in [
    'static bool wadaUnsignedField(', 'static meshtastic_Config_LoRaConfig_RegionCode wadaSelectedRegion(',
    'static void wadaPopulateRegions(', 'static bool wadaPresetChoice(', 'static void wadaPopulatePresets(',
    'static void wadaPresetChanged(', 'static void wadaRegionChanged(',
    'static void wadaPresetFieldsChanged(', 'static void wadaSaveRadio(']))
exe=build/'native-preset-ui'
subprocess.run(['clang++','-std=c++17',*flags,f'-I{build}',f'-I{root}/lib/WadaApp/port',
                f'-I{root}/src',f'-I{root}/src/mesh/generated',f'-I{root}/.pio/libdeps/t-deck-tft/Nanopb',
                str(test/'test_native_preset_ui.cpp'),str(root/'lib/WadaApp/port/WadaNativeConfig.cpp'),
                '-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)

(build/'native_node_actions.inc').write_text('\n'.join(function(name) for name in [
    'static bool hexToPubkey32(', 'static void addContactSubmitCb(',
    'static void actionSheetFavoriteCb(', 'static void actionSheetDeleteCb(']))
(build/'native_node_removal.inc').write_text(function('void UITask::onMeshtasticNodeRemoval('))
(build/'native_node_filter_label.inc').write_text(function('static const char* contactsFilterShortLabel('))
(build/'native_node_tick.inc').write_text(function('static void wadaNativeNodeTick('))
exe=build/'native-node-ui'
subprocess.run(['clang++','-std=c++17',*flags,f'-I{build}',f'-I{root}/lib/WadaApp/port',
                f'-I{ui}/ui-touch',f'-I{root}/src',f'-I{root}/src/mesh/generated',f'-I{root}/.pio/libdeps/t-deck-tft/Nanopb',
                str(test/'test_native_node_ui.cpp'),str(root/'lib/WadaApp/port/WadaNodeState.cpp'),
                '-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)

# Real status renderer/editor: raw Notes rows and UTF-8 byte-safe local drafts.
exe=build/'native-status-ui'
subprocess.run(['clang++','-std=c++17',*flags,f'-I{root}/lib/WadaApp/port',f'-I{ui}/ui-touch',
                str(test/'test_native_status_ui.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)

# Actual Apply callbacks must validate without truncation or changing hidden native enum values.
(build/'native_settings_actions.inc').write_text('\n'.join(function(name,settings) for name in [
    'static void wadaSaveIdentity(', 'static void wadaToggleWifi(', 'static void wadaWifiFieldsChanged(', 'static void wadaSaveWifi(', 'static void wadaSaveBluetooth(',
    'static void wadaKeyModeChanged(', 'static void wadaLoadChannel(', 'static bool wadaChannelUsesPublicKey(', 'static void wadaSaveChannel(',
    'static void wadaSavePosition(', 'static void wadaApplyGps(', 'static void wadaUseLiveGps(',
    'static void wadaSaveBroadcasts(']))
(build/'native_quick_ui.inc').write_text('\n'.join(function(name) for name in [
    'bool UITask::getGPSState(', 'void UITask::toggleGPS(', 'bool UITask::isBleEnabled(',
    'void UITask::disableBle(', 'bool UITask::enableBle(']))
exe=build/'native-settings-actions'
subprocess.run(['clang++','-std=c++17',*flags,f'-I{build}',f'-I{root}/lib/WadaApp/port',
                f'-I{root}/src',f'-I{root}/src/mesh/generated',f'-I{root}/.pio/libdeps/t-deck-tft/Nanopb',
                '-DWADA_MESHTASTIC=1',str(test/'test_native_settings_actions.cpp'),str(root/'lib/WadaApp/port/WadaNativeConfig.cpp'),
                '-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)

# Actual receive entry point and terminal submit: classification cannot inherit a prior notification.
(build/'native_receive_ui.inc').write_text(function('void UITask::newMsgFromPubWithMeta('))
receive=function('void UITask::newMsgImpl(')
receive=receive[:receive.index('  // For channel/group messages')]
(build/'native_receive_dispatch.inc').write_text(function('static bool wadaNativeChannelMuted(')+receive+
    '  recordDelivery(thread,channel,meta_flags,native_muted);\n}\n')

(build/'native_terminal_submit.inc').write_text(function('static void terminalSubmit('))
send_guard=function('static void termDoSend(')
(build/'native_terminal_send_guard.inc').write_text(function('static size_t arabicUnshape(')+function('static bool wadaNormalizeMessage(')+send_guard[:send_guard.index('  uint32_t ts =')]+
    '  ++sendCalls;\n}\n')
guards=''
for signature,end in [('bool luaHostMeshSendDM(', '  ContactInfo* by ='),
                      ('bool luaHostMeshSendChannel(', '  for (int i ='),
                      ('bool UITask::sendComposerToActiveThread(', '  if (_active_thread_is_channel)')]:
    body=function(signature)
    guards += body[:body.index(end)]+'  ++luaSendCalls; return true;\n}\n'
(build/'native_message_guards.inc').write_text(guards)
second=source[source.index('#else\nstatic void sendFromPanelCb('):]
(build/'native_composer_input_guards.inc').write_text(function('static void composerSendFromPanel(')+
    function('static void sendFromPanelCb(',second))
exe=build/'native-receive-ui'
subprocess.run(['clang++','-std=c++17',*flags,f'-I{build}',f'-I{root}/lib/WadaApp/port',
                str(test/'test_native_receive_ui.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)
print('PASS: native receive classification and complete UTF-8 terminal input',flush=True)

# Desktop committed text prefers an explicitly tapped field; native keyboard policy is unchanged.
block=function('static void bleKbdDispatch(',source[source.index('static bool bleKbdTouchHighlightShown('):])
start=block.index('  if (key & BLE_KEY_TEXT) {\n    lv_obj_t* ta = bleKbdTouchTypingTarget();')
end=block.index('  // The textarea being edited, if any:',start)
(build/'desktop_committed_ui.inc').write_text(function('static void bleKbdUtf8(')+
    function('static lv_obj_t* bleKbdTouchTappedField(')+'static void dispatchCommitted(int key) {\n'+block[start:end]+'}\n')
for flavor in ['desktop','native']:
    exe=build/('committed-text-'+flavor)
    subprocess.run(['clang++','-std=c++17',*flags,f'-I{build}',*(['-DWADA_DESKTOP=1'] if flavor=='desktop' else []),
                    str(test/'test_desktop_text_ui.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)

# A queued local-send anchor must survive a fast ACK refresh until scroll application.
start=source.index('  if (p.follow_own_send) scroll_target = LV_COORD_MAX;')
end=source.index('  chatVirtQueueScroll(&p, scroll_target);',start)+len('  chatVirtQueueScroll(&p, scroll_target);')
(build/'native_follow_scroll.inc').write_text(function('static void chatVirtApplyPendingScroll(')+
    'static void queueRefresh(LvChatPanel& p,int scroll_target) {\n#if defined(WADA_MESHTASTIC)\n'+source[start:end]+'\n}\n')
exe=build/'native-follow-scroll'
subprocess.run(['clang++','-std=c++17',*flags,f'-I{build}',str(test/'test_native_follow_scroll.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,timeout=30)
