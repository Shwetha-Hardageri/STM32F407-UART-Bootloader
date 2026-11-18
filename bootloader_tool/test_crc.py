import struct, zlib
print(hex(zlib.crc32(b"123456789")))        # must print 0xcbf43926
body = bytes([1, 0])                         # CMD_GET_VER, no payload
print((bytes([0xA5]) + body + struct.pack("<I", zlib.crc32(body))).hex(" "))