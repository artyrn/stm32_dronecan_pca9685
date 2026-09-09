#!/usr/bin/env python3

import time
import dronecan

TARGET_NODE_ID = 42
ACTUATOR_ID = 1

node = dronecan.make_node(
    "can0",
    node_id=100,
    bitrate=500000,
)


def request_sync(request, timeout=2.0):
    result = {"done": False, "event": None}

    def callback(event):
        result["done"] = True
        result["event"] = event

    node.request(
        request,
        TARGET_NODE_ID,
        callback,
        timeout=1.0,
    )

    deadline = time.monotonic() + timeout

    while not result["done"] and time.monotonic() < deadline:
        node.spin(0.05)

    return result["event"]


def write_param(name, value):
    req = dronecan.uavcan.protocol.param.GetSet.Request()
    req.name = name
    req.value.integer_value = value

    event = request_sync(req)

    if event is None:
        print(f"{name}: TIMEOUT")
        return False

    try:
        returned = event.response.value.integer_value
    except (AttributeError, TypeError):
        print(f"{name}: WRITE FAILED")
        return False

    print(f"{name}: requested={value}, returned={returned}")
    return returned == value


def send_level(pwm, duration):
    msg = dronecan.uavcan.equipment.actuator.ArrayCommand()

    cmd = dronecan.uavcan.equipment.actuator.Command()
    cmd.actuator_id = ACTUATOR_ID
    cmd.command_type = 4
    cmd.command_value = float(pwm)

    msg.commands.append(cmd)

    deadline = time.monotonic() + duration
    next_tx = time.monotonic()

    while time.monotonic() < deadline:
        now = time.monotonic()

        if now >= next_tx:
            node.broadcast(msg)
            next_tx += 0.05       # 20 Hz

        node.spin(0.005)


try:
    print("Configure OUT01 as ON_OFF")
    print()

    if not write_param("OUT01_TYPE", 2):
        raise SystemExit(1)

    if not write_param("OUT01_ON", 2000):
        raise SystemExit(1)

    if not write_param("OUT01_OFF", 1000):
        raise SystemExit(1)

    print()
    print("=== TEST 1: FS_STATE = 0 ===")

    if not write_param("OUT01_FS_STATE", 0):
        raise SystemExit(1)

    print("Sending ON (2000 us) for 3 seconds...")
    print("Expected while receiving: ~0.32 V")

    send_level(2000, 3.0)

    print()
    print("Transmission STOPPED.")
    print("FS_TIMEOUT = 700 ms")
    print("Expected after ~0.7 s: OFF = ~0.16 V")

    time.sleep(3.0)

    input("Measure output. Press Enter for TEST 2...")

    print()
    print("=== TEST 2: FS_STATE = 1 ===")

    if not write_param("OUT01_FS_STATE", 1):
        raise SystemExit(1)

    print("Sending OFF (1000 us) for 3 seconds...")
    print("Expected while receiving: ~0.16 V")

    send_level(1000, 3.0)

    print()
    print("Transmission STOPPED.")
    print("FS_TIMEOUT = 700 ms")
    print("Expected after ~0.7 s: ON = ~0.32 V")

    time.sleep(3.0)

    input("Measure output. Press Enter...")

    print()
    print("Failsafe test finished.")
    print("No SAVE was performed.")

finally:
    node.close()
