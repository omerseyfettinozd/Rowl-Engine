#!/usr/bin/env python3
import socket
import struct
import time

def test_ipc():
    socket_path = "/tmp/rowl_engine_ipc.sock"
    print(f"[IPC Tester] Connecting to Unix Domain Socket: {socket_path}")

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        s.connect(socket_path)
        print("[IPC Tester] Connected successfully to C++ Engine IPC Server!")

        # Packet: Magic 'ROWL' (4 bytes), Type 2 (uint16 = UpdateNodeGraph), Payload Size (uint32)
        payload = b'{"node_id":101, "dialogue":"Live Preview Hot-Reload Sync Test Success!"}'
        msg_type = 2
        payload_size = len(payload)

        # Header format: 4s H I (4 bytes magic, uint16 type, uint32 size)
        header = struct.pack('<4sHI', b'ROWL', msg_type, payload_size)

        s.sendall(header + payload)
        print(f"[IPC Tester] Sent UpdateNodeGraph packet ({payload_size} bytes) to Engine!")

        time.sleep(0.5)
        s.close()
    except Exception as e:
        print(f"[IPC Tester] Error: {e}")

if __name__ == '__main__':
    test_ipc()
