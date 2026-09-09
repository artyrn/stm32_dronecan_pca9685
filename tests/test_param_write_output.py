#!/usr/bin/env python3

import time
import dronecan

TARGET_NODE_ID = 42
PARAM_NAME = "OUT01_FS"
NEW_VALUE = 1300

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


def read_param(name):
    req = dronecan.uavcan.protocol.param.GetSet.Request()
    req.name = name

    event = request_sync(req)

    if event is None:
        print(f"{name}: TIMEOUT")
        return None

    try:
        value = event.response.value.integer_value
    except (AttributeError, TypeError):
        print(f"{name}: NOT FOUND")
        return None

    print(f"{name}: {value}")
    return value


def write_param(name, value):
    req = dronecan.uavcan.protocol.param.GetSet.Request()
    req.name = name
    req.value.integer_value = value

    event = request_sync(req)

    if event is None:
        print(f"WRITE {name}: TIMEOUT")
        return False

    try:
        returned = event.response.value.integer_value
    except (AttributeError, TypeError):
        print(f"WRITE {name}: FAILED")
        return False

    print(f"WRITE {name}: requested={value}, returned={returned}")
    return returned == value


def save_config():
    req = dronecan.uavcan.protocol.param.ExecuteOpcode.Request()

    # DroneCAN ExecuteOpcode: SAVE = 0
    req.opcode = 0
    req.argument = 0

    event = request_sync(req)

    if event is None:
        print("SAVE: TIMEOUT")
        return False

    print(
        f"SAVE: ok={event.response.ok} "
        f"argument={event.response.argument}"
    )

    return bool(event.response.ok)


try:
    print("Before:")
    old_value = read_param(PARAM_NAME)

    if old_value is None:
        raise SystemExit(1)

    print()

    if not write_param(PARAM_NAME, NEW_VALUE):
        raise SystemExit(1)

    print()

    print("After write:")
    if read_param(PARAM_NAME) != NEW_VALUE:
        print("ERROR: readback mismatch")
        raise SystemExit(1)

    print()

    if not save_config():
        print("ERROR: SAVE failed")
        raise SystemExit(1)

    print()
    print("Write + SAVE completed successfully.")

finally:
    node.close()
