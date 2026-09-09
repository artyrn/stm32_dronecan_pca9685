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


def send_pwm(pwm, duration=10.0):
    msg = dronecan.uavcan.equipment.actuator.ArrayCommand()

    cmd = dronecan.uavcan.equipment.actuator.Command()
    cmd.actuator_id = ACTUATOR_ID
    cmd.command_type = 4
    cmd.command_value = float(pwm)

    msg.commands.append(cmd)

    print(f"Sending PWM={pwm} at 20 Hz for {duration:.1f} s")

    deadline = time.monotonic() + duration
    next_tx = time.monotonic()

    while time.monotonic() < deadline:
        now = time.monotonic()

        if now >= next_tx:
            node.broadcast(msg)
            next_tx += 0.05       # 20 Hz

        node.spin(0.005)

try:
    print("Configure OUT01 in RAM only:")

    if not write_param("OUT01_TYPE", 2):
        raise SystemExit(1)

    if not write_param("OUT01_ON", 2000):
        raise SystemExit(1)

    if not write_param("OUT01_OFF", 1000):
        raise SystemExit(1)

    print()
    print("OFF test")
    send_pwm(1000, 10.0)

    input("Press Enter for ON test...")

    print()
    print("ON test")
    send_pwm(2000, 10.0)

    input("Press Enter for final OFF test...")

    print()
    print("OFF test")
    send_pwm(1000, 10.0)

finally:
    node.close()
