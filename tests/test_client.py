import socket
import struct
import sys

# 對應 C++ enum CommandID
CMD_PUBLISH_MESSAGE = 2001

def recv_all(sock, n):
    """一個輔助函式，用於確保從 socket 接收到 n 個位元組的資料。"""
    data = bytearray()
    while len(data) < n:
        packet = sock.recv(n - len(data))
        if not packet:
            # 如果在收完所有預期資料前連線就中斷，這是一個錯誤
            raise ConnectionAbortedError("Socket connection broken before all data was received")
        data.extend(packet)
    return bytes(data)

def main():
    if len(sys.argv) < 2:
        print("Usage: python test_client.py \"Your message here\"")
        return

    message = sys.argv[1].encode('utf-8')
    payload_len = len(message)
    header_len = 8
    total_len = header_len + payload_len
    
    # 建立 Header
    # !IHBB -> Network Endian(!), uint32, uint16, uint8, uint8
    header = struct.pack('!IHBB', total_len, CMD_PUBLISH_MESSAGE, 0, 0)
    
    packet = header + message

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.connect(('127.0.0.1', 12345))
            print(f"Connected to server.")
            
            # 發送封包
            print(f"Sending message: '{message.decode()}'")
            s.sendall(packet)
            
            # 接收回傳的封包
            # 1. 接收 Header
            reply_header_data = recv_all(s, header_len)

            reply_total_len, reply_cmd_id, _, _ = struct.unpack('!IHBB', reply_header_data)
            reply_payload_len = reply_total_len - header_len
            
            print(f"Received header: total_len={reply_total_len}, cmd_id={reply_cmd_id}")
            
            # 2. 接收 Payload
            if reply_payload_len > 0:
                reply_payload_data = recv_all(s, reply_payload_len)
            else:
                reply_payload_data = b''
            
            print(f"Received echo: '{reply_payload_data.decode()}'")

            assert message == reply_payload_data

            print("Echo SUCCESS!")
        
        except (ConnectionRefusedError, ConnectionAbortedError) as e:
            print(f"Connection error: {e}")
            print("Is the server running and accessible?")
        except Exception as e:
            print(f"An error occurred: {e}")

if __name__ == '__main__':
    main()