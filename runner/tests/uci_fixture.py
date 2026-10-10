import queue
import subprocess
import threading
import time


class engine_session:
    def __init__(self, command, directory):
        self.process = subprocess.Popen(command, cwd=directory, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, bufsize=1)
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=self.read_output, daemon=True)
        self.reader.start()

    def read_output(self):
        for line in self.process.stdout:
            self.lines.put((time.monotonic(), line.strip()))
        self.lines.put((time.monotonic(), None))

    def send(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def receive(self, deadline):
        if time.monotonic() >= deadline:
            raise RuntimeError("engine response timed out")
        try:
            timestamp, line = self.lines.get(timeout=max(0, deadline - time.monotonic()))
        except queue.Empty:
            raise RuntimeError("engine response timed out")
        if line is None:
            raise RuntimeError("engine exited before completing UCI response")
        return timestamp, line

    def wait_for(self, marker, timeout):
        deadline = time.monotonic() + timeout
        lines = []
        while True:
            _, line = self.receive(deadline)
            lines.append(line)
            if line == marker:
                return lines

    def close(self):
        try:
            if self.process.poll() is None:
                self.send("quit")
                self.process.wait(timeout=2)
        except (OSError, subprocess.TimeoutExpired):
            self.process.kill()
        finally:
            self.process.wait()
            self.reader.join(timeout=2)
            self.process.stdin.close()
            self.process.stdout.close()
