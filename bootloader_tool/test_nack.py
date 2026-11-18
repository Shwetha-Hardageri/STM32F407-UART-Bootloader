import sys
import time
import struct
import serial
from flash_tool import send_cmd, CMD_ERASE, CMD_WRITE, CMD_GET_VER, ACK, NACK, PORT, BAUD

ser = serial.Serial(PORT, BAUD, timeout=3)
time.sleep(2)               # Uno resets when the port opens
ser.reset_input_buffer()

failures = 0


def expect_nack(name, cmd, payload):
    """Send one packet that the bootloader must reject."""
    global failures
    try:
        reply = send_cmd(ser, cmd, payload)
    except TimeoutError:
        print(f"FAIL  {name}: no reply (bootloader may have hard-faulted)")
        ser.close()
        sys.exit(1)

    ok = (reply == NACK)
    failures += (not ok)
    print(f"{'PASS' if ok else 'FAIL'}  {name}: reply {hex(reply)} (expected {hex(NACK)})")


def write_pkt(addr, data):
    return struct.pack("<I", addr) + data


# ---------------- ERASE must be rejected ----------------
expect_nack("erase sector 0-1 (bootloader)", CMD_ERASE, bytes([0, 2]))
expect_nack("erase sector 1 only (bootloader)", CMD_ERASE, bytes([1, 1]))
expect_nack("erase 2+11 sectors (past sector 11)", CMD_ERASE, bytes([2, 11]))
expect_nack("erase 11+2 sectors (past sector 11)", CMD_ERASE, bytes([11, 2]))
expect_nack("erase count 0", CMD_ERASE, bytes([2, 0]))
expect_nack("erase wrong payload length", CMD_ERASE, bytes([2]))

# ---------------- WRITE must be rejected ----------------
expect_nack("write to bootloader area 0x08000000", CMD_WRITE,
            write_pkt(0x08000000, bytes(8)))
expect_nack("write just below app 0x08007FFC", CMD_WRITE,
            write_pkt(0x08007FFC, bytes(4)))
expect_nack("write to 0xFFFFFFFC (address wrap-around)", CMD_WRITE,
            write_pkt(0xFFFFFFFC, bytes(4)))
expect_nack("write past end of flash 0x08100000", CMD_WRITE,
            write_pkt(0x08100000, bytes(4)))
expect_nack("write crossing end of flash 0x080FFFFC + 8", CMD_WRITE,
            write_pkt(0x080FFFFC, bytes(8)))
expect_nack("write to RAM address 0x20000000", CMD_WRITE,
            write_pkt(0x20000000, bytes(4)))
expect_nack("write unaligned address 0x08008001", CMD_WRITE,
            write_pkt(0x08008001, bytes(4)))
expect_nack("write length not a multiple of 4", CMD_WRITE,
            write_pkt(0x08008000, bytes(5)))
expect_nack("write with no data", CMD_WRITE,
            struct.pack("<I", 0x08008000))

# ---------------- bootloader must still be alive ----------------
try:
    alive = send_cmd(ser, CMD_GET_VER) == ACK
except TimeoutError:
    alive = False
failures += (not alive)
print(f"{'PASS' if alive else 'FAIL'}  bootloader still answers GET_VER")

ser.close()

print()
print("ALL TESTS PASSED" if failures == 0 else f"{failures} TEST(S) FAILED")
sys.exit(1 if failures else 0)