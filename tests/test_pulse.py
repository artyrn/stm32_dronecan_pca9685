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

    print(
        f"Sending PWM={pwm} at 20 Hz "
        f"for {duration:.1f} s"
    )

    deadline = time.monotonic() + duration
    next_tx = time.monotonic()

    while time.monotonic() < deadline:
        now = time.monotonic()

        if now >= next_tx:
            node.broadcast(msg)
            next_tx += 0.05

        node.spin(0.005)


try:
    print("Configure OUT01 as PULSE in RAM only:")
    print()

    if not write_param("OUT01_TYPE", 3):
        raise SystemExit(1)

    if not write_param("OUT01_ON", 2000):
        raise SystemExit(1)

    if not write_param("OUT01_OFF", 1000):
        raise SystemExit(1)

    if not write_param("OUT01_TIME", 2000):
        raise SystemExit(1)

    if not write_param("OUT01_RETRIG", 0):
        raise SystemExit(1)

    print()
    print("NO SAVE - configuration is RAM only.")
    print()

    print("STEP 1: establish OFF")
    send_level(1000, 2.0)

    input("Expected ~0.16 V. Press Enter...")

    print()
    print("STEP 2: OFF -> ON")
    print(
        "Expected ~0.32 V for about 2 seconds, "
        "then ~0.16 V although ON continues."
    )
    send_level(2000, 6.0)

    input(
        "Pulse should now be finished. "
        "Press Enter while input is still logically ON..."
    )

    print()
    print("STEP 3: send ON again WITHOUT OFF")
    print(
        "RETRIG=IGNORE: expected NO new pulse, "
        "output stays ~0.16 V."
    )
    send_level(2000, 3.0)

    input("Press Enter to re-arm with OFF...")

    print()
    print("STEP 4: send OFF")
    send_level(1000, 2.0)

    input(
        "Now OFF has re-armed the pulse. "
        "Press Enter for second OFF -> ON..."
    )

    print()
    print("STEP 5: second OFF -> ON")
    print(
        "Expected another ~2 second pulse: "
        "~0.32 V -> ~0.16 V."
    )
    send_level(2000, 5.0)

    print()
    print("PULSE RETRIG=IGNORE test finished.")

finally:
    node.close()
