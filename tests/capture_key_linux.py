"""Capture the production Tk fallback windows on an X11 desktop (e.g. Xvfb).
Only synthetic local TLS fixture keys are used; no user certificates or readings.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import tkinter as tk

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_key_launchers import KeyLauncherTests

images=ROOT/'docs/images';images.mkdir(exist_ok=True)
source=(ROOT/'tools/key-export/obtener-clave-nightscout.sh').read_text()
input_code=re.search(r"tk\) URL=\$\(python3 - <<'PY'\n(.*?)\nPY",source,re.S)[1]
result_code=re.search(r"elif \[ \"\$DIALOG\" = tk \]; then python3 - \"\$DEST\" <<'PY'\n(.*?)\nPY",source,re.S)[1]
old_wait=tk.Misc.wait_window

def capture_dialog(self, window=None):
    self.entry.delete(0,'end');self.entry.insert(0,'https://tu-nightscout.example/')
    def capture():
        self.update_idletasks()
        subprocess.run(['import','-display',self.winfo_screen(),'-window',hex(self.winfo_id()),str(images/'clave-linux.png')],check=True)
        self.cancel()
    self.after(250,capture)
    return old_wait(self,window)

tk.Misc.wait_window=capture_dialog
try:
    try:exec(compile(input_code,'production-input-window','exec'),{})
    except SystemExit as exc:
        if exc.code != 1:raise
finally:tk.Misc.wait_window=old_wait

KeyLauncherTests.setUpClass()
try:
    case=KeyLauncherTests('test_shell_export_and_rejection')
    with tempfile.TemporaryDirectory(prefix='nightscout-ejemplo-') as directory:
        desktop=Path(directory)/'Escritorio';desktop.mkdir()
        with case.server('rsa') as port:
            result=case.run_tool('shell',f'https://localhost:{port}/',desktop,case.fixtures['rsa'][0])
        if result.returncode:raise RuntimeError(result.stderr)
        file=next(desktop.glob('*.txt'))
        example=ROOT/'docs/examples/clave-publica-ejemplo.txt';example.parent.mkdir(exist_ok=True)
        shutil.copyfile(file,example)
        old_loop=tk.Misc.mainloop
        def capture_result(self,*args,**kwargs):
            def capture():
                self.update_idletasks()
                subprocess.run(['import','-display',self.winfo_screen(),'-window',hex(self.winfo_id()),str(images/'clave-linux-resultado.png')],check=True)
                self.destroy()
            self.after(250,capture)
            return old_loop(self,*args,**kwargs)
        tk.Misc.mainloop=capture_result
        previous=sys.argv;sys.argv=['production-result-window',str(file)]
        try:exec(compile(result_code,'production-result-window','exec'),{})
        finally:sys.argv=previous;tk.Misc.mainloop=old_loop
finally:KeyLauncherTests.doClassCleanups()
print('Captured actual Linux Tk input/result windows')
