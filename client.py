#! /usr/local/bin/python3

from socket import *
from struct import *

server_path = "/tmp/statsd_server.sock"
client_path = "/tmp/dumbclient.sock"

sock = socket(AF_UNIX, SOCK_DGRAM)
sock.bind(client_path)
init_data = pack("i", 0)
sock.sendto(init_data, server_path)
buf, _ = sock.recvfrom(2048)
print(buf)
t, uid, _ = unpack("ii2040s", buf)
print(t)
print(uid)

for i in range(100):
  sock.sendto(pack("iidd", 2, uid, i, i), server_path)
  sock.recvfrom(2048)

sock.sendto(pack("ii", 3, uid), server_path)
buf, _ = sock.recvfrom(2048)
_, _, avg, _, var, _, mn, _, mx, _, graph = unpack("iidddddddd1976s", buf)
print(avg)
print(var)
print(mn)
print(mx)
print(graph.decode("utf-8"))



