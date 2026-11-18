
import time, serial
from flash_tool import send_cmd, CMD_GET_VER, PORT, BAUD

ser = serial.Serial(PORT, BAUD, timeout=3)
time.sleep(2)  # Uno resets when the port opens

print("reply:", hex(send_cmd(ser, CMD_GET_VER)))
print("version byte:", ser.read(1))

ser.close()
