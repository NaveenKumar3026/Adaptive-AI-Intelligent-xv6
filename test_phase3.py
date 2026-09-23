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

    # Launch Phase 3 test workloads:
    # 1. stable (baseline predictability)
    # 2. bursttest (trend tracking)
    # 3. anomaly (burst surges & erratic churn)
    # 4. starve (wait accumulation & healing)
    commands = [
        "stable &\n",
        "bursttest &\n",
        "anomaly &\n",
        "starve &\n",
        "monitor 5 8\n",
        "kill 4\n",
        "kill 5\n",
        "kill 6\n",
        "kill 7\n",
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
        if time.time() - start_time > 50:
            p.terminate()
            break

    p.wait()
    print("\n--- Phase 3 Intelligent Prediction & Self-Healing Validation Test Complete ---")

if __name__ == "__main__":
    main()
