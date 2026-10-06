#!/usr/bin/env python3
"""Real macOS sockets exercise the production route probe, without an iPad."""
from pathlib import Path
import json
import socket
import subprocess
import tempfile
import threading

root = Path(__file__).resolve().parents[2]
source = (root / 'upstreams/Madeira/app/Madeira/JITNetwork.swift').read_text().split('import NetworkExtension')[0]
harness = r'''
@main struct Tests {
 static func main() throws {
  let a = CommandLine.arguments
  let result = AnyPS5TunnelRouteProbe.check(
    expectedInterface:a[2], expectedIndex:UInt32(a[3])!,
    expectedLocalAddress:a[4], targetAddress:"127.0.0.1", targetPort:UInt16(a[1])!,
    requireEmbeddedTunnel:a[5] == "1")
  print(String(data:try JSONEncoder().encode(result),encoding:.utf8)!)
 }
}
'''
with tempfile.TemporaryDirectory(prefix='anyps5-route-') as temp:
    path = Path(temp)
    (path / 'probe.swift').write_text(source + harness)
    binary = path / 'probe'
    subprocess.run(['xcrun', 'swiftc', '-swift-version', '5', '-parse-as-library',
                    str(path / 'probe.swift'), '-o', str(binary)], check=True, timeout=60)
    index = socket.if_nametoindex('lo0')
    def probe(port, interface='lo0', route_index=index, address='127.0.0.1', embedded=False):
        return json.loads(subprocess.check_output(
            [str(binary), str(port), interface, str(route_index), address, '1' if embedded else '0'],
            text=True, timeout=5))
    def listening(**options):
        with socket.socket() as server:
            server.bind(('127.0.0.1', 0)); server.listen(1); server.settimeout(0.7)
            observed = {'connections': 0, 'payload': 0}
            def accept():
                try:
                    connection, _ = server.accept()
                    with connection:
                        observed['connections'] += 1
                        connection.settimeout(1)
                        observed['payload'] += len(connection.recv(4096))
                except socket.timeout:
                    pass
            worker = threading.Thread(target=accept); worker.start()
            actual = probe(server.getsockname()[1], **options)
            worker.join(2); assert not worker.is_alive()
            return actual, observed
    actual, observed = listening()
    assert actual['reachable'] and actual['tcpAttempted'] and actual['milliseconds'] < 400
    assert observed == {'connections': 1, 'payload': 0}, 'probe must send no protocol request'
    for options in [dict(interface='utun999'), dict(route_index=index+999),
                    dict(address='10.7.1.1'), dict(embedded=True)]:
        actual, observed = listening(**options)
        assert not actual['reachable'] and not actual['tcpAttempted']
        assert observed == {'connections': 0, 'payload': 0}, 'wrong VPN/proxy must not be contacted'
    with socket.socket() as closed:
        closed.bind(('127.0.0.1', 0)); port = closed.getsockname()[1]
    actual = probe(port)
    assert not actual['reachable'] and actual['tcpAttempted'] and actual['milliseconds'] < 400
print('PASS 6 production route cases: exact interface/index/address, no payload, refusal, bounded connect')
