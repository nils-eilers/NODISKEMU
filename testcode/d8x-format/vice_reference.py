"""Format an image through PET BASIC and the real drive ROM in VICE.

No c1541 formatting, filesystem device, or IEC/IEEE KERNAL traps are used.
Commodore ROM files must be installed separately in VICE's ROM search path.
"""
from pathlib import Path
import re
import shutil
import socket
import subprocess
import time


def receive(sock):
    data = b''
    while not (re.search(rb'>C:7ffd\s', data, re.I) and
               re.search(rb'\(C:\$[0-9a-fA-F]+\)\s*$', data)):
        try:
            chunk = sock.recv(65536)
        except TimeoutError as error:
            raise RuntimeError(f'Incomplete VICE monitor response: {data!r}') from error
        if not chunk:
            break
        data += chunk
    return data.decode(errors='replace')


def format_image(image, command, timeout=45):
    image = Path(image).resolve()
    if not shutil.which('xpet'):
        raise RuntimeError('xpet is not installed')
    drive = '8050' if image.suffix.lower() == '.d80' else '8250'
    # keybuf uses ASCII lowercase for unshifted PETSCII uppercase letters.
    keys = ('10 open15,8,15,"'+command.lower()+'"\n'
            '20 input#15,e,e$,t,s\n30 close15\n'
            '40 poke32766,e:poke32765,42\nrun\n')
    keys = keys.replace('\n', r'\n')
    with socket.socket() as reserve:
        reserve.bind(('127.0.0.1', 0))
        port = reserve.getsockname()[1]
    logfile = image.with_suffix(image.suffix+'.vice.log')
    args = ['xpet','-default','-console','+sound','-warp','-model','8032',
            '-drive8type',drive,'-drive8truedrive','-drive8idle','0','+trapdevice8',
            '-8',str(image),'-keybuf',keys,'-remotemonitor',
            '-remotemonitoraddress',f'ip4://127.0.0.1:{port}']
    transcript = image.with_suffix(image.suffix+'.monitor.log')
    with logfile.open('w') as output:
        proc = subprocess.Popen(args, stdout=output, stderr=output)
        deadline = time.monotonic()+timeout
        observations = []
        sock = None
        try:
            while time.monotonic() < deadline:
                if proc.poll() is not None:
                    raise RuntimeError(f'VICE exited ({proc.returncode}); see {logfile}')
                time.sleep(0.25)
                if sock is None:
                    try:
                        sock = socket.create_connection(('127.0.0.1',port),timeout=2)
                    except ConnectionRefusedError:
                        continue
                    sock.settimeout(3)
                sock.sendall(b'm $7ffd $7ffe\n')
                result = receive(sock)
                observations.append(result)
                match = re.search(r'>C:7ffd\s+([0-9a-f]{2})\s+([0-9a-f]{2})',result,re.I)
                if match and int(match[1],16) == 42:
                    error = int(match[2],16)
                    sock.sendall(b'quit\n')
                    proc.wait(timeout=10)
                    if error:
                        raise RuntimeError(f'Commodore DOS error {error}; see {logfile}')
                    if proc.returncode:
                        raise RuntimeError(f'VICE exit status {proc.returncode}')
                    return logfile
                sock.sendall(b'x\n')
            raise TimeoutError(f'Commodore format did not finish; see {logfile}')
        finally:
            if sock is not None:
                sock.close()
            transcript.write_text('\n'.join(observations))
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
