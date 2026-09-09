import subprocess
import time
import sys

def main():
    qemu_cmd = [
        "qemu-system-riscv64",
        "-machine", "virt",
        "-bios", "none",
        "-kernel", "kernel/kernel",
        "-m", "128M",
        "-smp", "3",
        "-nographic",
        "-global", "virtio-mmio.force-legacy=false",
        "-drive", "file=fs.img,if=none,format=raw,id=x0",
        "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0"
    ]

    p = subprocess.Popen(
        qemu_cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    # Test each command sequentially to ensure zero exec failures
    commands = [
        "ls\n",
        "echo === TEST ECHO ===\n",
        "cat README\n",
        "cpu_test 20000000\n",
        "sleep_test 3 2\n",
        "mixed_test 2\n",
        "monitor\n",
        "\x01x"
    ]

    def feed():
        time.sleep(2)
        for cmd in commands:
            p.stdin.write(cmd)
            p.stdin.flush()
            time.sleep(2)

    import threading
    t = threading.Thread(target=feed)
    t.daemon = True
    t.start()

    start_time = time.time()
    while True:
        line = p.stdout.readline()
        if not line:
            break
        sys.stdout.write(line)
        sys.stdout.flush()
        if time.time() - start_time > 35:
            p.terminate()
            break

    p.wait()
    print("\n--- Direct execution test complete ---")

if __name__ == "__main__":
    main()
