#!/usr/bin/env python3
# fcitx5-wetype 真机桌面会话 e2e — 最终版
# 流程: SetCurrentIM(wetype-im) → 逐键 n-i-h-a-o → 空格上屏 → 断言 CommitString == 你好
import dbus, sys, time
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
CTL = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', '/controller'),
                     'org.fcitx.Fcitx.Controller1')
IM = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', '/org/freedesktop/portal/inputmethod'),
                    'org.fcitx.Fcitx.InputMethod1')

# 等引擎 dbus 就绪
ok = False
for _ in range(10):
    try:
        CTL.CurrentInputMethod(); ok = True; break
    except Exception:
        time.sleep(1)
if not ok:
    print('E2E_RESULT=FAIL (dbus 未就绪)'); sys.exit(1)

print('group current = wetype-im (profile)', flush=True)

committed = []
preedits = []
bus.add_signal_receiver(lambda *a, **k: preedits.append(str(a)[:160]),
                        dbus_interface='org.fcitx.Fcitx.InputContext1',
                        signal_name='UpdatePreedit', path_keyword='path')
bus.add_signal_receiver(lambda *a, **k: preedits.append('UpdateClientSideUI ' + str(a)[:120]),
                        dbus_interface='org.fcitx.Fcitx.InputContext1',
                        signal_name='UpdateClientSideUI', path_keyword='path')
bus.add_signal_receiver(lambda *a, **k: committed.append(str(a[0]) if a else ''),
                        dbus_interface='org.fcitx.Fcitx.InputContext1',
                        signal_name='CommitString', path_keyword='path')
CTL.SetCurrentIM('wetype-im')
time.sleep(0.5)
print('SetCurrentIM ->', CTL.CurrentInputMethod(), flush=True)
CAP = dbus.UInt64(1 | (1 << 39))   # ClientSideUI | ClientSideInputPanel
path, uuid = IM.CreateInputContext([('program', 'wetype-e2e'), ('capability', str(CAP))])
ic = dbus.Interface(bus.get_object('org.fcitx.Fcitx5', path), 'org.fcitx.Fcitx.InputContext1')
ic.FocusIn()
time.sleep(0.5)

loop = GLib.MainLoop()
keys = {'n': 110, 'i': 105, 'h': 104, 'a': 97, 'o': 111}
consumed = 0
for ch in 'nihao':
    r = ic.ProcessKeyEvent(dbus.UInt32(keys[ch]), dbus.UInt32(0), dbus.UInt32(0),
                           dbus.Boolean(False), dbus.UInt32(int(time.time()*1000) & 0xFFFFFFFF))
    consumed += bool(r)
    print(f'key {ch} consumed={r}', flush=True)
    for _ in range(10):
        loop.get_context().iteration(False)
        time.sleep(0.05)
print('consumed_total =', consumed, flush=True)
print('PREEDIT_SIGNALS =', len(preedits), preedits[:2], flush=True)
time.sleep(1)
loop.get_context().iteration(False)
print('PREEDIT_SIGNALS =', len(preedits), preedits[:3], flush=True)
print('--- 空格上屏 ---', flush=True)
ic.ProcessKeyEvent(dbus.UInt32(32), dbus.UInt32(0), dbus.UInt32(0),
                   dbus.Boolean(False), dbus.UInt32(int(time.time()*1000) & 0xFFFFFFFF))
t0 = time.time()
while time.time() - t0 < 12:
    loop.get_context().iteration(False)
    time.sleep(0.1)
    if committed: break
print('COMMITTED =', committed, flush=True)
passed = bool(consumed) and bool(committed) and '你好' in committed[0]
print('E2E_RESULT =', 'PASS' if passed else 'FAIL', flush=True)
sys.exit(0 if passed else 1)
