#!/usr/bin/env python3

import time
import dronecan

NODE_ID = 42

node = dronecan.make_node(
    "can0",
    node_id=100,
    bitrate=500000,
)


def wait_request(request, target):
    result = {"done": False, "event": None}

    def callback(event):
        result["done"] = True
        result["event"] = event

    node.request(
        request,
        target,
        callback,
        timeout=1.0,
    )

    deadline = time.monotonic() + 2.0

    while not result["done"] and time.monotonic() < deadline:
        node.spin(0.05)

    return result["event"]


def read_param(name):
    req = dronecan.uavcan.protocol.param.GetSet.Request()
    req.name = name

    event = wait_request(req, NODE_ID)

    if event is None:
        print(f"{name}: TIMEOUT")
        return None

    print(f"{name}: {event.response.value}")
    return event.response


def write_param(name, value):
    req = dronecan.uavcan.protocol.param.GetSet.Request()
    req.name = name
    req.value.integer_value = value

    event = wait_request(req, NODE_ID)

    if event is None:
        print(f"{name}: WRITE TIMEOUT")
        return False

    print(f"{name}: after write = {event.response.value}")
    return True


def save_config():
    req = dronecan.uavcan.protocol.param.ExecuteOpcode.Request()
    req.opcode = 0   # OPCODE_SAVE
    req.argument = 0

    event = wait_request(req, NODE_ID)

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
    read_param("FS_TIMEOUT")

    print("\nWrite RAM:")
    write_param("FS_TIMEOUT", 700)

    print("\nRead after write:")
    read_param("FS_TIMEOUT")

    print("\nSave:")
    save_config()

finally:
    node.close()
