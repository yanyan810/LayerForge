import json
import sys
import time

with open(sys.argv[1], "r", encoding="utf-8") as stream:
    json.load(stream)
for progress in range(0, 101, 10):
    print(f"[Progress] {progress}", flush=True)
    time.sleep(0.05)
print("complete", flush=True)
