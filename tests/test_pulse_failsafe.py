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
    print("Configure OUT01: PULSE + FAILSAFE")
    print()

    params = (
        ("OUT01_TYPE", 3),       # PULSE
        ("OUT01_ON", 2000),
        ("OUT01_OFF", 1000),
        ("OUT01_TIME", 5000),    # 5 s pulse
        ("OUT01_RETRIG", 0),     # IGNORE
        ("OUT01_FS_STATE", 0),   # failsafe -> OFF
    )

    for name, value in params:
        if not write_param(name, value):
            raise SystemExit(1)

    print()
    print("NO SAVE - configuration is RAM only.")
    print()

    # First establish a real OFF state.
    print("STEP 1: establish OFF")
    print("Expected: ~0.16 V")

    send_level(1000, 2.0)

    input("Press Enter to start 5 second pulse...")

    print()
    print("STEP 2: start PULSE")
    print("OUT01_TIME = 5000 ms")
    print("Expected: output -> ~0.32 V")
    print()
    print("ON commands will be sent for ONLY 1 second.")
    print("Then all actuator transmission stops.")
    print()

    send_level(2000, 1.0)

    print()
    print("========== TRANSMISSION STOPPED ==========")
    print()
    print("Pulse timer would normally run for 5 seconds.")
    print("But FS_TIMEOUT = 700 ms.")
    print()
    print("Expected:")
    print("  immediately after STOP : ~0.32 V")
    print("  about 0.7 s later      : ~0.16 V")
    print("  OLED                   : FS ON")
    print()

    # Important: do NOT spin/send anything here that could
    # generate actuator commands.
    time.sleep(4.0)

    print()
    print("If output is now ~0.16 V, failsafe interrupted PULSE.")

    input("Press Enter for FS_STATE=1 test...")

    # ----------------------------------------------------------
    # Second test: failsafe ON
    # ----------------------------------------------------------

    print()
    print("STEP 3: configure FS_STATE = 1")

    if not write_param("OUT01_FS_STATE", 1):
        raise SystemExit(1)

    # Re-arm pulse with OFF.
    print("Establish OFF / re-arm...")
    send_level(1000, 2.0)

    print()
    print("Start 5 second pulse, transmit ON for 1 second...")
    send_level(2000, 1.0)

    print()
    print("========== TRANSMISSION STOPPED ==========")
    print()
    print("FS_STATE=1")
    print("Expected after ~700 ms: output remains ~0.32 V")
    print("OLED should show FS ON.")
    print()

    time.sleep(4.0)

    input("Measure output and press Enter...")

    print()
    print("PULSE FAILSAFE test finished.")
    print("No SAVE was performed.")

finally:
    node.close()
