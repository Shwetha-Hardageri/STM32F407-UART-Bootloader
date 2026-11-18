import time, struct, serial
from flash_tool import send_cmd, CMD_ERASE, CMD_WRITE, PORT, BAUD, APP_ADDR

ser = serial.Serial(PORT, BAUD, timeout=3)
time.sleep(2)
ser.timeout = 40
print("erase:", hex(send_cmd(ser, CMD_ERASE, bytes([2, 10]))))
ser.timeout = 3
payload = struct.pack("<I", APP_ADDR) + bytes([1, 2, 3, 4, 5, 6, 7, 8])
print("write:", hex(send_cmd(ser, CMD_WRITE, payload)))
ser.close()