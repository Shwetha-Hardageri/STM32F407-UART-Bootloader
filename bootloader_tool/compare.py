a = open("app.bin", "rb").read()
b = open("read.bin", "rb").read()

print("Original size:", len(a))
print("Readback size:", len(b))
print("Match:", b[:len(a)] == a)
print("Extra bytes:", b[len(a):])