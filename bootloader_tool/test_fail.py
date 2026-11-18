import time, struct, zlib, serial
from flash_tool import SOF, CMD_GET_VER, PORT, BAUD

def raw_packet(cmd, payload=b"", flip_crc=False):
    body = bytes([cmd, len(payload)]) + payload
    crc = zlib.crc32(body) & 0xFFFFFFFF
    if flip_crc:
        crc ^= 0x1                       # flip one bit of the CRC
    return bytes([SOF]) + body + struct.pack("<I", crc)

ser = serial.Serial(PORT, BAUD, timeout=3)
time.sleep(2)

ser.write(raw_packet(CMD_GET_VER, flip_crc=True))
r = ser.read(1)
print("bad CRC reply (expect 0x1f):", hex(r[0]) if r else "timeout")

ser.reset_input_buffer()
ser.write(raw_packet(CMD_GET_VER))
print("good packet right after (expect 79 01):", ser.read(2).hex(" "))
ser.close()