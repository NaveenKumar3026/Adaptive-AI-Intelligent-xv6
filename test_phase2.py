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

    # Launch cpu_test (infinite compute), sleep_test (interactive), and mixed_test (hybrid),
    # then observe the dynamic priority scores and time-slice allocations in monitor.
    commands = [
        "cpu_test &\n",
        "sleep_test &\n",
        "mixed_test &\n",
        "monitor 4 12\n",
        "kill 4\n",
        "kill 5\n",
        "kill 6\n",
        "monitor 1 5\n",
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
        if time.time() - start_time > 45:
            p.terminate()
            break

    p.wait()
    print("\n--- Phase 2 Adaptive Scheduler Validation Test Complete ---")

if __name__ == "__main__":
    main()
