"""Link the .nwa files in extra_apps/ for the free app space of slot B and
pack them into one DFU file (apps.B.dfu)."""
import glob, os, subprocess, sys

nm_file, out_dir = sys.argv[1], sys.argv[2]
nm = open(nm_file).read()
symbols = {}
for line in nm.splitlines():
    parts = line.split()
    if len(parts) == 3:
        symbols[parts[2]] = int(parts[0], 16)

flash_start = symbols["_external_apps_flash_start"]
flash_end = symbols["_external_apps_flash_end"]
ram_start = symbols["_external_apps_RAM_start"]
ram_end = symbols["_external_apps_RAM_end"]
# Same formula as nwlink: the trampoline follows the first two ISR entries
trampoline = symbols["InitialisationVector"] + 8
print(f"apps flash {flash_start:#x}-{flash_end:#x}  ram {ram_start:#x}-{ram_end:#x}  trampoline {trampoline:#x}")

SECTOR = 0x10000
apps = sorted(glob.glob("extra_apps/*.nwa"))
blob = bytearray()
address = flash_start
for app in apps:
    out = os.path.join(out_dir, os.path.basename(app) + ".bin")
    subprocess.run(["npx", "--yes", "nwlink@latest", "nwa-bin",
                    "-fs", hex(address), "-fl", hex(flash_end - address),
                    "-rs", hex(ram_start), "-rl", hex(ram_end - ram_start),
                    "-ts", hex(trampoline), app, out], check=True)
    data = open(out, "rb").read()
    offset = address - flash_start
    blob += b"\xff" * (offset - len(blob))
    blob += data
    print(f"{os.path.basename(app)}: {len(data)} bytes at {address:#x}")
    address = flash_start + ((len(blob) + SECTOR - 1) // SECTOR) * SECTOR
    if address > flash_end:
        sys.exit(f"Not enough space: the apps need {len(blob)} bytes, there are {flash_end - flash_start}")

free = flash_end - flash_start - len(blob)
print(f"Used {len(blob)} bytes, still free: {free} bytes ({free / 1024 / 1024:.2f} MB)")
if blob:
    with open(os.path.join(out_dir, "apps.B.bin"), "wb") as f:
        f.write(blob)
    subprocess.run([sys.executable, "tools/device/bin2dfu.py", "-i", os.path.join(out_dir, "apps.B.bin"),
                    "-s", hex(flash_start), "-o", os.path.join(out_dir, "apps.B.dfu"),
                    "-u", "0x0483:0xa291"], check=True)
