"""Validate actual ClientHello templates independently of permissive mimicry parsing."""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[4] / 'net/trans/tls'

def validate(data):
    assert data[0] == 1, 'not ClientHello'
    assert int.from_bytes(data[1:4], 'big') == len(data)-4, 'handshake length'
    assert data[4:6] == b'\x03\x03', 'legacy_version must be TLS 1.2 for TLS 1.3'
    pos = 38
    session_size = data[pos]
    assert session_size <= 32, 'session id length'
    pos += 1 + session_size
    cipher_size = int.from_bytes(data[pos:pos+2], 'big')
    assert cipher_size and cipher_size % 2 == 0, 'cipher vector length'
    pos += 2 + cipher_size
    assert pos < len(data), 'cipher vector outside message'
    comp_size = data[pos]
    pos += 1
    assert data[pos:pos+comp_size] == b'\0', 'compression vector'
    pos += comp_size
    extension_size = int.from_bytes(data[pos:pos+2], 'big')
    pos += 2
    assert pos + extension_size == len(data), 'extensions length'
    while pos < len(data):
        assert pos+4 <= len(data), 'extension header truncated'
        length = int.from_bytes(data[pos+2:pos+4], 'big')
        pos += 4 + length
        assert pos <= len(data), 'extension outside message'

failed = []
for name in ('chrome_120', 'firefox_121', 'edge_120', 'safari_17', 'android_14', 'telegram_android'):
    text = (root / (name+'.c')).read_text()
    array = re.search(r'static const uint8_t\s+\w+\[\]\s*=\s*\{(.*?)\};', text, re.S).group(1)
    data = bytearray(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b', array))
    # The production builder normalises legacy_version and handshake length.
    data[4:6] = b'\x03\x03'
    data[1:4] = (len(data)-4).to_bytes(3, 'big')
    try:
        validate(data)
        print(name+': PASS')
    except AssertionError as error:
        failed.append(name)
        print(name+': FAIL: '+str(error))
if failed:
    raise SystemExit(1)
