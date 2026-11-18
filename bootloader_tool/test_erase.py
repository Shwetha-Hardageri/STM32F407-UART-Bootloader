import time, serial
from flash_tool import send_cmd, CMD_ERASE, PORT, BAUD

ser = serial.Serial(PORT, BAUD, timeout=3)
time.sleep(2)
ser.timeout = 40
print("erase:", hex(send_cmd(ser, CMD_ERASE, bytes([2, 10]))))
ser.close()