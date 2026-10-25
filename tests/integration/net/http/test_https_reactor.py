"""Run inside Debian builder with /work/build containing a built node.
Reuse its exact compile/link dependencies for the standalone SDK listener.
"""
import pathlib
import shlex
import socket
import ssl
import subprocess
import tempfile
import time

build = pathlib.Path('/work/build')
source = pathlib.Path(__file__).with_name('https_probe_runner.c')
commands = subprocess.check_output(['ninja', '-C', str(build), '-t', 'commands', 'cellframe-node'], text=True).splitlines()
compile_cmd = next(shlex.split(line) for line in commands if ' -c ' in line and '/sources/cellframe-node.c' in line)
link_line = next(line for line in reversed(commands) if ' -o cellframe-node ' in line)
link_cmd = shlex.split(link_line.split('&&')[1])
with tempfile.TemporaryDirectory() as directory:
    tmp = pathlib.Path(directory)
    obj, exe = tmp/'runner.o', tmp/'runner'
    compile_cmd[compile_cmd.index('-o')+1] = str(obj)
    compile_cmd[compile_cmd.index('-c')+1] = str(source)
    for flag in ('-MF', '-MT'):
        if flag in compile_cmd:
            index = compile_cmd.index(flag)
            del compile_cmd[index:index+2]
    subprocess.run(compile_cmd, cwd=build, check=True)
    link_cmd = [arg for arg in link_cmd if not arg.endswith('.c.o')]
    link_cmd.insert(1, str(obj))
    link_cmd[link_cmd.index('-o')+1] = str(exe)
    subprocess.run(link_cmd, cwd=build, check=True)
    subprocess.run(['openssl','req','-x509','-newkey','ec','-pkeyopt','ec_paramgen_curve:P-256',
        '-nodes','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost','-days','1',
        '-keyout',str(tmp/'key.pem'),'-out',str(tmp/'cert.pem')], check=True)
    with socket.socket() as reserve:
        reserve.bind(('127.0.0.1',0)); port=reserve.getsockname()[1]
    with (tmp/'server.log').open('w+') as log:
        server=subprocess.Popen([str(exe),str(tmp),str(tmp/'cert.pem'),str(tmp/'key.pem'),str(port)],
            stdin=subprocess.PIPE,stdout=log,stderr=log,text=True)
        try:
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                log.flush()
                if 'HTTPS_READY' in (tmp/'server.log').read_text(): break
                if server.poll() is not None: raise RuntimeError((tmp/'server.log').read_text())
                time.sleep(.1)
            else: raise TimeoutError('listener startup')
            context=ssl.create_default_context(cafile=str(tmp/'cert.pem'))
            context.minimum_version=ssl.TLSVersion.TLSv1_3
            for protocols in (['http/1.1'], []):
                context.set_alpn_protocols(protocols)
                with socket.create_connection(('127.0.0.1',port),timeout=10) as raw:
                    with context.wrap_socket(raw,server_hostname='localhost') as client:
                        assert client.selected_alpn_protocol()==('http/1.1' if protocols else None)
                        for part in (b'GET / HTTP/1.1\r',b'\nHost: localhost\r\n',b'\r\n'):
                            client.sendall(part);time.sleep(.02)
                        response=b''
                        while True:
                            part=client.recv(4096)
                            if not part: break
                            response+=part
                        assert response.startswith(b'HTTP/1.1 200 OK\r\n'),response
                        assert response.split(b'\r\n\r\n',1)[1]==b'https-ok\n',response
                        client.unwrap().close()
            context.set_alpn_protocols(['http/1.1'])
            for delay in (0, 1):
                with socket.create_connection(('127.0.0.1',port),timeout=15) as raw:
                    with context.wrap_socket(raw,server_hostname='localhost') as client:
                        client.sendall(b'GET /bulk HTTP/1.1\r\nHost: localhost\r\n\r\n')
                        time.sleep(delay)
                        chunks=[]
                        while True:
                            part=client.recv(4096)
                            if not part: break
                            chunks.append(part)
                        header,body=b''.join(chunks).split(b'\r\n\r\n',1)
                        assert header.startswith(b'HTTP/1.1 200 OK'),header
                        assert b'Content-Length: 1048576' in header,header
                        assert body==bytes(range(251))*(1048576//251)+bytes(range(1048576%251)),len(body)
                        client.unwrap().close()
            context.set_alpn_protocols(['h2'])
            with socket.create_connection(('127.0.0.1',port),timeout=10) as raw:
                try:
                    unexpected=context.wrap_socket(raw,server_hostname='localhost')
                except ssl.SSLError:
                    pass
                else:
                    unexpected.close()
                    raise AssertionError('h2-only client unexpectedly accepted')
            # Memory BIO client lets us withhold our close_notify after receiving
            # the server's alert, while retaining the underlying TCP socket.
            context.set_alpn_protocols(['http/1.1'])
            with socket.create_connection(('127.0.0.1',port),timeout=10) as wire:
                incoming,outgoing=ssl.MemoryBIO(),ssl.MemoryBIO()
                tls=context.wrap_bio(incoming,outgoing,server_side=False,server_hostname='localhost')
                while True:
                    try:
                        tls.do_handshake()
                        if outgoing.pending: wire.sendall(outgoing.read())
                        break
                    except ssl.SSLWantReadError:
                        if outgoing.pending: wire.sendall(outgoing.read())
                        packet=wire.recv(4096)
                        assert packet,'EOF during handshake'
                        incoming.write(packet)
                tls.write(b'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n')
                if outgoing.pending: wire.sendall(outgoing.read())
                reply=b''
                started=time.monotonic()
                while True:
                    try:
                        part=tls.read(4096)
                        if not part: break
                        reply+=part
                    except ssl.SSLWantReadError:
                        if outgoing.pending: wire.sendall(outgoing.read())
                        packet=wire.recv(4096)
                        assert packet,'TCP closed before TLS alert'
                        incoming.write(packet)
                    except ssl.SSLZeroReturnError:
                        break
                assert reply.split(b'\r\n\r\n',1)[1]==b'https-ok\n',reply
                # Intentionally do not unwrap or flush a reciprocal TLS alert.
                while True:
                    try:
                        packet=wire.recv(4096)
                    except ConnectionResetError:
                        break
                    if not packet: break
                elapsed=time.monotonic()-started
                assert 3 <= elapsed <= 10,elapsed
            # No ClientHello: the real handshake deadline must retire TCP.
            with socket.create_connection(('127.0.0.1',port),timeout=15) as stalled:
                started=time.monotonic()
                assert stalled.recv(1)==b''
                assert 7 <= time.monotonic()-started <= 15
            context.set_alpn_protocols(['http/1.1'])
            # Established TLS, unfinished request: total probe deadline bounds it.
            with socket.create_connection(('127.0.0.1',port),timeout=35) as raw:
                with context.wrap_socket(raw,server_hostname='localhost') as stalled:
                    started=time.monotonic()
                    stalled.sendall(b'GET / HTTP/1.1\r\nHost:')
                    assert stalled.recv(1)==b''
                    assert 24 <= time.monotonic()-started <= 35
            # Abort a bulk response without TLS close_notify.
            raw=socket.create_connection(('127.0.0.1',port),timeout=10)
            aborted=context.wrap_socket(raw,server_hostname='localhost')
            aborted.sendall(b'GET /bulk HTTP/1.1\r\nHost: localhost\r\n\r\n')
            aborted.close()
            # Stop accepting while a handshaken connection still owns the router.
            raw=socket.create_connection(('127.0.0.1',port),timeout=10)
            survivor=context.wrap_socket(raw,server_hostname='localhost')
            server.stdin.write('\n');server.stdin.flush()
            deadline=time.monotonic()+5
            while 'HTTPS_STOPPED' not in (tmp/'server.log').read_text():
                if time.monotonic()>deadline: raise TimeoutError('listener stop')
                time.sleep(.05)
            try:
                survivor.sendall(b'GET / HTTP/1.1\r\nHost: localhost\r\n\r\n')
                response=b''
                while True:
                    part=survivor.recv(4096)
                    if not part: break
                    response+=part
                assert response.split(b'\r\n\r\n',1)[1]==b'https-ok\n',response
                survivor.unwrap().close()
            finally:
                survivor.close()
            server.communicate('\n',timeout=15)
            assert server.returncode==0
            print('PASS: SDK HTTPS small/bulk, ALPN, close/handshake/request deadlines, abort and live-connection listener stop')
        except Exception:
            print((tmp/'server.log').read_text())
            raise
        finally:
            if server.poll() is None: server.kill();server.wait()
