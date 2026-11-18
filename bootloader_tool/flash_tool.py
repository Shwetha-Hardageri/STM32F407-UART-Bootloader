import sys
import time
import struct
import zlib
import argparse
import os
import serial

PORT = os.environ.get("BOOTLOADER_PORT", "COM7")   # also imported by the test scripts
BAUD = 57600

SOF, ACK, NACK = 0xA5, 0x79, 0x1F

CMD_GET_VER = 1
CMD_ERASE = 2
CMD_WRITE = 3
CMD_JUMP = 4

APP_ADDR = 0x08008000
CHUNK = 124


def send_cmd(ser, cmd, payload=b""):
    body = bytes([cmd, len(payload)]) + payload
    crc = zlib.crc32(body) & 0xFFFFFFFF

    ser.write(bytes([SOF]) + body + struct.pack("<I", crc))

    r = ser.read(1)

    if not r:
        raise TimeoutError("No response from bootloader")

    return r[0]


def main(binfile, port):

    with open(binfile, "rb") as f:
        data = f.read()

    if not data:
        sys.exit("Application binary is empty")

    data += b"\xFF" * (-len(data) % 4)

    ser = serial.Serial(port, BAUD, timeout=3)

    try:
        time.sleep(2)  # Uno resets when the port opens
        ser.reset_input_buffer()

        # ---------------- GET VERSION ----------------

        if send_cmd(ser, CMD_GET_VER) != ACK:
            sys.exit("Bootloader not responding")

        version = ser.read(1)

        if not version:
            sys.exit("No bootloader version received")

        print("Bootloader version:", version[0])
        print(f"Image size: {len(data)} bytes")

        # ---------------- ERASE ----------------

        print("Erasing sectors 2..11 (takes several seconds)...")

        ser.timeout = 40

        t0 = time.perf_counter()

        if send_cmd(ser, CMD_ERASE, bytes([2, 10])) != ACK:
            sys.exit("Erase failed")

        t1 = time.perf_counter()

        ser.timeout = 3

        print(f"Erase time: {t1 - t0:.3f} seconds")

        # ---------------- WRITE ----------------

        print("Writing firmware...")

        t_write_start = time.perf_counter()

        for off in range(0, len(data), CHUNK):

            payload = struct.pack("<I", APP_ADDR + off) + data[off:off + CHUNK]

            for attempt in range(3):

                try:
                    if send_cmd(ser, CMD_WRITE, payload) == ACK:
                        break

                except TimeoutError:
                    pass

                time.sleep(0.3)
                ser.reset_input_buffer()

            else:
                sys.exit(f"\nWrite failed at offset 0x{off:X}")

            print(
                f"\r{min(off + CHUNK, len(data)) * 100 // len(data)}%",
                end="",
                flush=True
            )

        write_time = time.perf_counter() - t_write_start
        throughput = (len(data) / 1024) / write_time

        print("\n")
        print(f"Write time: {write_time:.3f} seconds")
        print(f"Throughput: {throughput:.2f} KiB/s")

        # ---------------- JUMP ----------------

        print("Jumping to application...")

        if send_cmd(ser, CMD_JUMP) != ACK:
            print("Jump rejected (app invalid)")
            sys.exit(1)

        print("OK")

    finally:
        ser.close()


if __name__ == "__main__":

    parser = argparse.ArgumentParser(
        description="STM32 UART Bootloader Flash Tool"
    )

    parser.add_argument("binfile", help="Application .bin file")

    parser.add_argument(
        "--port",
        default=PORT,
        help="Serial port (default: BOOTLOADER_PORT environment variable or COM7)"
    )

    args = parser.parse_args()

    try:
        main(args.binfile, args.port)
    except FileNotFoundError as e:
        sys.exit(f"File not found: {e.filename}")
    except TimeoutError as e:
        sys.exit(f"Timeout: {e}")
    except serial.SerialException as e:
        sys.exit(f"Serial port error: {e}")