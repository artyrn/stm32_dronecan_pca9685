#include <stdint.h>
#include <stdbool.h>
#include "status.h"
#include "canard.h"
#include "canard_stm32.h"
#include "uavcan.protocol.NodeStatus.h"
#include "uavcan.protocol.GetNodeInfo_res.h"
#include "uavcan.equipment.actuator.ArrayCommand.h"
#include "board.h"
#include "pca9685.h"
#include "can_hw.h"
#include "config.h"
#include "param_server.h"

#define CANARD_MEMORY_SIZE   1024U


static float actuator_value[ACTUATOR_COUNT];
static uint8_t actuator_valid[ACTUATOR_COUNT];
static uint32_t last_actuator_command_us = 0U;
static uint8_t have_actuator_command = 0U;

/*
 * PULSE runtime state.
 *
 * pulse_input_on:
 *   Last logical state received from DroneCAN.
 *
 * pulse_active:
 *   Output is currently ON and its timer is running.
 *
 * pulse_wait_off:
 *   RETRIG=IGNORE pulse has completed. A new pulse is
 *   blocked until a real OFF command is received.
 */
static uint8_t pulse_input_on[ACTUATOR_COUNT];
static uint8_t pulse_active[ACTUATOR_COUNT];
static uint8_t pulse_wait_off[ACTUATOR_COUNT];
static uint32_t pulse_started_us[ACTUATOR_COUNT];

static uint32_t last_pca_recovery_us[PCA9685_DEVICE_COUNT];

static uint8_t pca_fault_active[PCA9685_DEVICE_COUNT];

static uint8_t actuator_failsafe_active = 0U;

static uint32_t actuator_rx_count = 0U;

uint32_t micros32(void);
uint64_t micros64(void);

static CanardInstance canard;
static uint8_t canard_memory[CANARD_MEMORY_SIZE];

static uint16_t pca_status_flag(uint8_t device)
{
    return (device == 0U) ?
        STATUS_FLAG_PCA0_ERROR :
        STATUS_FLAG_PCA1_ERROR;
}


static uint16_t output_state_pwm(
    const output_config_t *out,
    uint8_t state_on)
{
    return (state_on != 0U) ?
        out->on_pwm_us :
        out->off_pwm_us;
}


static void pulse_reset(uint8_t actuator)
{
    pulse_input_on[actuator] = 0U;
    pulse_active[actuator] = 0U;
    pulse_wait_off[actuator] = 0U;
    pulse_started_us[actuator] = 0U;
}


static int write_output_pwm(uint8_t actuator,
                            uint16_t pwm_us)
{
    const uint8_t device =
        (uint8_t)(
            actuator /
            PCA9685_CHANNELS_PER_DEVICE);

    const uint8_t channel =
        (uint8_t)(
            actuator %
            PCA9685_CHANNELS_PER_DEVICE);

    if (pca9685_set_pwm_us(
            device,
            channel,
            pwm_us) < 0) {

        pca_fault_active[device] = 1U;

        status_set_flags(
            pca_status_flag(device));

        return -1;
    }

    return 0;
}


static void pulse_process(void)
{
    /*
     * Failsafe owns all outputs while active.
     * It also resets the pulse state when it enters.
     */
    if (actuator_failsafe_active != 0U) {
        return;
    }

    const uint32_t now = micros32();
    const uint8_t output_count =
        config_get_output_count();

    for (uint8_t actuator = 0U;
         actuator < output_count;
         actuator++) {

        const output_config_t *out =
            config_get_output(actuator);

        if ((out == 0) ||
            (out->type != OUTPUT_PULSE) ||
            (pulse_active[actuator] == 0U)) {
            continue;
        }

        const uint32_t pulse_time_us =
            out->pulse_time_ms * 1000U;

        if ((uint32_t)(
                now - pulse_started_us[actuator]) <
            pulse_time_us) {
            continue;
        }

        (void)write_output_pwm(
            actuator,
            out->off_pwm_us);

        pulse_active[actuator] = 0U;

        if ((out->pulse_retrigger == PULSE_IGNORE) &&
            (pulse_input_on[actuator] != 0U)) {

            pulse_wait_off[actuator] = 1U;
        }
    }
}


static void pca_recovery_process(void)
{
    const uint32_t now = micros32();

    const config_t *cfg = config_get();

    for (uint8_t device = 0U;
         device < cfg->pca_count;
         device++) {
     
        if (pca_fault_active[device] == 0U) {
            continue;
        }

        if ((uint32_t)(
                now - last_pca_recovery_us[device]) <
            PCA_RECOVERY_INTERVAL_US) {
            continue;
        }

        last_pca_recovery_us[device] = now;

        if (pca9685_recover(device) < 0) {
            status_set_flags(
                pca_status_flag(device));
            continue;
        }

        uint8_t recovery_ok = 1U;

        /*
         * PCA9685 reset loses channel state.
         * Restore either safe PWM or last
         * received actuator values.
         */
        for (uint8_t channel = 0U;
             channel < PCA9685_CHANNELS_PER_DEVICE;
             channel++) {

            const uint8_t actuator =
                (uint8_t)(
                    device *
                    PCA9685_CHANNELS_PER_DEVICE +
                    channel);

            const output_config_t *out =
                config_get_output(actuator);

            uint16_t pwm_us =
                CONFIG_PWM_NEUTRAL_US;

            if (out != 0) {

                if (out->type == OUTPUT_PWM) {
                    pwm_us =
                        out->failsafe_pwm_us;

                } else if ((out->type == OUTPUT_ON_OFF) ||
                           (out->type == OUTPUT_PULSE)) {

                    pwm_us =
                        output_state_pwm(
                            out,
                            out->failsafe_state);
                }
            }

            if ((out != 0) &&
                (actuator_failsafe_active == 0U) &&
                (actuator_valid[actuator] != 0U)) {

                if (out->type == OUTPUT_PWM) {

                    float pwm =
                        actuator_value[actuator];

                    if (pwm < 500.0F) {
                        pwm = 500.0F;
                    }

                    if (pwm > 2500.0F) {
                        pwm = 2500.0F;
                    }

                    pwm_us = (uint16_t)pwm;

                } else if (out->type == OUTPUT_ON_OFF) {

                    pwm_us =
                        output_state_pwm(
                            out,
                            actuator_value[actuator] >= 1500.0F);

                } else if (out->type == OUTPUT_PULSE) {

                    pwm_us =
                        (pulse_active[actuator] != 0U) ?
                        out->on_pwm_us :
                        out->off_pwm_us;
                }
            }

            if (pca9685_set_pwm_us(
                    device,
                    channel,
                    pwm_us) < 0) {

                recovery_ok = 0U;
                break;
            }
        }

        if (recovery_ok != 0U) {

            pca_fault_active[device] = 0U;

            status_clear_flags(
                pca_status_flag(device));

        } else {

            pca_fault_active[device] = 1U;

            status_set_flags(
                pca_status_flag(device));
        }
    }
}


static void actuator_failsafe_process(void)
{
    const uint32_t now = micros32();

    const config_t *cfg = config_get();

    const uint32_t failsafe_timeout_us =
        (uint32_t)cfg->failsafe_timeout_ms * 1000U;

    const uint8_t timed_out =
        (have_actuator_command == 0U) ||
        ((uint32_t)(now - last_actuator_command_us) >=
         failsafe_timeout_us);

    if (timed_out == 0U) {

        if (actuator_failsafe_active != 0U) {
                actuator_failsafe_active = 0U;
                status_clear_flags(STATUS_FLAG_FAILSAFE);
        }

        return;
    }
    /*
     * Safe values have already been written.
     */
    if (actuator_failsafe_active != 0U) {
        return;
    }

    const uint8_t output_count =
        config_get_output_count();

    for (uint8_t actuator = 0U;
         actuator < output_count;
         actuator++) {
     
        const uint8_t device =
            (uint8_t)(
                actuator /
                PCA9685_CHANNELS_PER_DEVICE);

        const uint8_t channel =
            (uint8_t)(
                actuator %
                PCA9685_CHANNELS_PER_DEVICE);

        const output_config_t *out =
            config_get_output(actuator);

        if (out == 0) {
            continue;
        }

        uint16_t safe_pwm_us;

        if (out->type == OUTPUT_PWM) {

            safe_pwm_us =
                out->failsafe_pwm_us;

        } else if ((out->type == OUTPUT_ON_OFF) ||
                   (out->type == OUTPUT_PULSE)) {

            safe_pwm_us =
                output_state_pwm(
                    out,
                    out->failsafe_state);

        } else {

            /*
             * DISABLED is not actively driven here.
             */
            pulse_reset(actuator);
            actuator_valid[actuator] = 0U;
            continue;
        }

        if (pca9685_set_pwm_us(
                device,
                channel,
                safe_pwm_us) < 0) {

            pca_fault_active[device] = 1U;

            status_set_flags(
                pca_status_flag(device));
        }

        /*
         * Do not pretend that the failsafe value
         * came from DroneCAN.
         */
        actuator_valid[actuator] = 0U;

        actuator_value[actuator] =
            (float)safe_pwm_us;

        /*
         * Failsafe has absolute priority over
         * any running or latched pulse.
         */
        pulse_reset(actuator);
    }

    actuator_failsafe_active = 1U;

    status_set_flags(
        STATUS_FLAG_FAILSAFE);
}


static uint8_t node_health_from_status(void)
{
    const uint16_t flags =
        status_get_flags();

    if ((flags & STATUS_FLAG_FATAL) != 0U) {

        return UAVCAN_PROTOCOL_NODESTATUS_HEALTH_CRITICAL;
    }

    if ((flags &
        (STATUS_FLAG_CAN_ERROR |
         STATUS_FLAG_PCA0_ERROR |
         STATUS_FLAG_PCA1_ERROR)) != 0U) {

        return UAVCAN_PROTOCOL_NODESTATUS_HEALTH_ERROR;
    }

    if ((flags &
        (STATUS_FLAG_FAILSAFE |
         STATUS_FLAG_OLED_ERROR)) != 0U) {

        return UAVCAN_PROTOCOL_NODESTATUS_HEALTH_WARNING;
    }

    return UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK;
}

static void send_get_node_info_response(CanardRxTransfer *transfer)
{
    struct uavcan_protocol_GetNodeInfoResponse response = {0};

    response.status.uptime_sec =
        (uint32_t)(micros64() / 1000000ULL);

    response.status.health = node_health_from_status();
    response.status.mode = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    response.status.sub_mode = 0U;
    response.status.vendor_specific_status_code = status_get_flags();

    /*
     * Software version 1.0.
     * Пока VCS commit и image CRC не передаем.
     */
    response.software_version.major = FW_VERSION_MAJOR;
    response.software_version.minor = FW_VERSION_MINOR;
    response.software_version.optional_field_flags = 0U;
    response.software_version.vcs_commit = 0U;
    response.software_version.image_crc = 0U;

    /*
     * Hardware version 1.0.
     */
    response.hardware_version.major = 1U;
    response.hardware_version.minor = 0U;

    /*
     * STM32F103 имеет 96-битный уникальный ID по адресу 0x1FFFF7E8.
     *
     * В DroneCAN требуется 128 бит.
     * Первые четыре байта используем как фиксированный префикс,
     * остальные 12 байт — UID самого STM32.
     */
    response.hardware_version.unique_id[0] = 'P';
    response.hardware_version.unique_id[1] = 'C';
    response.hardware_version.unique_id[2] = '3';
    response.hardware_version.unique_id[3] = '2';

    const volatile uint8_t *stm32_uid =
        (const volatile uint8_t *)0x1FFFF7E8UL;

    for (uint8_t i = 0U; i < 12U; i++) {
        response.hardware_version.unique_id[4U + i] =
            stm32_uid[i];
    }

    /*
     * Certificate of authenticity пока отсутствует.
     */
    response.hardware_version.certificate_of_authenticity.len = 0U;

    static const char node_name[] = "com.artyrn.pca9685x2";

    response.name.len = sizeof(node_name) - 1U;

    for (uint8_t i = 0U; i < response.name.len; i++) {
        response.name.data[i] = (uint8_t)node_name[i];
    }

    uint8_t buffer[UAVCAN_PROTOCOL_GETNODEINFO_RESPONSE_MAX_SIZE];

    const uint32_t payload_len =
        uavcan_protocol_GetNodeInfoResponse_encode(
            &response,
            buffer);

    /*
     * Для response Transfer-ID должен совпасть с запросом.
     */
    uint8_t transfer_id = transfer->transfer_id;
    const uint8_t destination_node_id =
        transfer->source_node_id;

    canardReleaseRxTransferPayload(&canard, transfer);

    (void)canardRequestOrRespond(
        &canard,
        destination_node_id,
        UAVCAN_PROTOCOL_GETNODEINFO_RESPONSE_SIGNATURE,
        UAVCAN_PROTOCOL_GETNODEINFO_RESPONSE_ID,
        &transfer_id,
        CANARD_TRANSFER_PRIORITY_LOW,
        CanardResponse,
        buffer,
        (uint16_t)payload_len);
}

static void handle_actuator_array_command(CanardRxTransfer *transfer);

static void on_transfer_received(CanardInstance *ins,
                                 CanardRxTransfer *transfer)
{
    (void)ins;

    if (param_server_handle(
            &canard,
            transfer)) {

        return;
    }    

    if ((transfer->transfer_type == CanardTransferTypeRequest) &&
        (transfer->data_type_id ==
         UAVCAN_PROTOCOL_GETNODEINFO_RESPONSE_ID)) {

        send_get_node_info_response(transfer);
        return;
    }

    if ((transfer->transfer_type == CanardTransferTypeBroadcast) &&
        (transfer->data_type_id ==
         UAVCAN_EQUIPMENT_ACTUATOR_ARRAYCOMMAND_ID)) {

        handle_actuator_array_command(transfer);
        return;
    }

}

static bool should_accept_transfer(const CanardInstance *ins,
                                   uint64_t *out_data_type_signature,
                                   uint16_t data_type_id,
                                   CanardTransferType transfer_type,
                                   uint8_t source_node_id)
{
  (void)ins;
  (void)source_node_id;
  
  if ((transfer_type == CanardTransferTypeBroadcast) &&
    (data_type_id == UAVCAN_EQUIPMENT_ACTUATOR_ARRAYCOMMAND_ID)) {

    *out_data_type_signature =
        UAVCAN_EQUIPMENT_ACTUATOR_ARRAYCOMMAND_SIGNATURE;

    return true;
  }
  
  if ((transfer_type == CanardTransferTypeRequest) &&
      (data_type_id == UAVCAN_PROTOCOL_GETNODEINFO_RESPONSE_ID)) {

      *out_data_type_signature =
          UAVCAN_PROTOCOL_GETNODEINFO_RESPONSE_SIGNATURE;

      return true;
  }

    if (param_server_should_accept(
            out_data_type_signature,
            data_type_id,
            transfer_type)) {

        return true;
    }

    return false;
}


static void handle_actuator_array_command(CanardRxTransfer *transfer)
{
    struct uavcan_equipment_actuator_ArrayCommand msg = {0};

    if (uavcan_equipment_actuator_ArrayCommand_decode(
            transfer,
            &msg)) {

        canardReleaseRxTransferPayload(
            &canard,
            transfer);

        return;
    }

    for (uint8_t i = 0U;
         i < msg.commands.len;
         i++) {

        const struct uavcan_equipment_actuator_Command *cmd =
            &msg.commands.data[i];

        const uint8_t output_count =
            config_get_output_count();

        if ((cmd->actuator_id < 1U) ||
            (cmd->actuator_id > output_count)) {
            continue;
        }
        
        const uint8_t actuator =
            (uint8_t)(cmd->actuator_id - 1U);

        if (cmd->command_type !=
            UAVCAN_EQUIPMENT_ACTUATOR_COMMAND_COMMAND_TYPE_PWM) {
            continue;
        }

        const output_config_t *out =
            config_get_output(actuator);

        if (out == 0) {
            continue;
        }

        /*
         * ArduPilot USE_ACTUATOR_PWM sends PWM command type
         * for every configured output. OUTxx_TYPE defines
         * how this node interprets the received PWM value.
         */
        const float input_pwm =
            cmd->command_value;

        actuator_value[actuator] =
            input_pwm;

        actuator_valid[actuator] = 1U;

        have_actuator_command = 1U;
        last_actuator_command_us =
            micros32();

        actuator_rx_count++;

        if (out->type == OUTPUT_DISABLED) {

            pulse_reset(actuator);
            actuator_valid[actuator] = 0U;
            continue;
        }

        if (out->type == OUTPUT_PWM) {

            float pwm = input_pwm;

            if (pwm < 500.0F) {
                pwm = 500.0F;
            }

            if (pwm > 2500.0F) {
                pwm = 2500.0F;
            }

            (void)write_output_pwm(
                actuator,
                (uint16_t)pwm);

            continue;
        }

        const uint8_t input_on =
            (input_pwm >= 1500.0F) ?
            1U :
            0U;

        if (out->type == OUTPUT_ON_OFF) {

            (void)write_output_pwm(
                actuator,
                output_state_pwm(
                    out,
                    input_on));

            continue;
        }

        if (out->type == OUTPUT_PULSE) {

            const uint8_t old_input_on =
                pulse_input_on[actuator];

            pulse_input_on[actuator] =
                input_on;

            if (input_on == 0U) {

                /*
                 * OFF rearms RETRIG=IGNORE.
                 * It does not abort an already running pulse;
                 * the pulse still runs for OUTxx_TIME.
                 */
                pulse_wait_off[actuator] = 0U;
                continue;
            }

            if (pulse_active[actuator] != 0U) {

                if (out->pulse_retrigger ==
                    PULSE_RESTART) {

                    pulse_started_us[actuator] =
                        micros32();
                }

                continue;
            }

            if (pulse_wait_off[actuator] != 0U) {
                continue;
            }

            /*
             * New pulse requires OFF -> ON.
             */
            if (old_input_on != 0U) {
                continue;
            }

            (void)write_output_pwm(
                actuator,
                out->on_pwm_us);

            pulse_active[actuator] = 1U;
            pulse_started_us[actuator] =
                micros32();

            continue;
        }
    }

    canardReleaseRxTransferPayload(
        &canard,
        transfer);
}


uint32_t dronecan_get_actuator_rx_count(void)
{
    return actuator_rx_count;
}


void dronecan_actuator_init(void)
{
    const uint16_t flags =
        status_get_flags();

    const config_t *cfg = config_get();

    pca_fault_active[0U] =
        ((flags & STATUS_FLAG_PCA0_ERROR) != 0U)
            ? 1U
            : 0U;

    if (cfg->pca_count >= 2U) {
        pca_fault_active[1U] =
            ((flags & STATUS_FLAG_PCA1_ERROR) != 0U)
                ? 1U
                : 0U;
    } else {
        pca_fault_active[1U] = 0U;
        status_clear_flags(STATUS_FLAG_PCA1_ERROR);
    }
    
    /*
     * No valid DroneCAN actuator command has
     * been received yet.
     *
     * The first actuator_process() call will
     * therefore enter failsafe and keep all
     * outputs at their safe values.
     */
    have_actuator_command = 0U;
    actuator_failsafe_active = 0U;

    for (uint8_t actuator = 0U;
         actuator < ACTUATOR_COUNT;
         actuator++) {

        actuator_valid[actuator] = 0U;
        actuator_value[actuator] = 0.0F;
        pulse_reset(actuator);
    }
}

void dronecan_actuator_process(void)
{
    actuator_failsafe_process();
    pulse_process();
    pca_recovery_process();
}


void dronecan_init(void)
{
    canardInit(&canard,
               canard_memory,
               sizeof(canard_memory),
               on_transfer_received,
               should_accept_transfer,
               0);

    canardSetLocalNodeID(
        &canard,
        config_get()->node_id);
}

static void send_node_status(void)
{
    struct uavcan_protocol_NodeStatus status = {0};

    status.uptime_sec = (uint32_t)(micros64() / 1000000ULL);
    status.health = node_health_from_status();
    status.mode = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    status.sub_mode = 0U;
    status.vendor_specific_status_code = status_get_flags();

    uint8_t buffer[UAVCAN_PROTOCOL_NODESTATUS_MAX_SIZE];

    const uint32_t length =
        uavcan_protocol_NodeStatus_encode(&status, buffer);

    static uint8_t transfer_id = 0U;

    (void)canardBroadcast(&canard,
                          UAVCAN_PROTOCOL_NODESTATUS_SIGNATURE,
                          UAVCAN_PROTOCOL_NODESTATUS_ID,
                          &transfer_id,
                          CANARD_TRANSFER_PRIORITY_LOW,
                          buffer,
                          (uint16_t)length);
}

void dronecan_process(void)
{
    static uint32_t last_status_us = 0U;
    static uint32_t last_cleanup_us = 0U;


    const uint32_t now = micros32();

    if ((uint32_t)(now - last_cleanup_us) >=
        CANARD_RECOMMENDED_STALE_TRANSFER_CLEANUP_INTERVAL_USEC) {

        last_cleanup_us = now;

        canardCleanupStaleTransfers(
            &canard,
            micros64());
    }

    if ((uint32_t)(now - last_status_us) >= 1000000U) {
        last_status_us = now;
        send_node_status();
    }

    /*
     * Transmit queued DroneCAN frames.
     */
    for (;;) {
        const CanardCANFrame *frame =
            canardPeekTxQueue(&canard);

        if (frame == 0) {
            break;
        }

        const int16_t result =
            canardSTM32Transmit(frame);

        if (result > 0) {

            canardPopTxQueue(&canard);

        } else if (result < 0) {

            /*
             * Hardware/driver TX error.
             *
             * Drop this frame so a permanently
             * failed frame cannot block the queue.
             */
            canardPopTxQueue(&canard);

        } else {

            /*
             * No TX mailbox available now.
             * This is not an error.
             */
            break;
        }
    }

    /*
     * Receive all currently available CAN frames.
     */
    CanardCANFrame rx_frame;

    for (;;) {
        const int16_t rx_result =
            canardSTM32Receive(&rx_frame);

        if (rx_result > 0) {


            const uint64_t rx_now =
                micros64();

            (void)canardHandleRxFrame(
                &canard,
                &rx_frame,
                rx_now);

            continue;
        }


        /*
         * rx_result == 0 means no frame available.
         */
        break;
    }

    /*
     * Current bxCAN hardware state.
     *
     * ABOM is enabled by libcanard, so BUS-OFF
     * recovery itself is performed by bxCAN.
     */
    const uint8_t can_status =
        can_hw_get_status();

    if ((can_status &
        (CAN_HW_STATUS_WARNING |
         CAN_HW_STATUS_PASSIVE |
         CAN_HW_STATUS_BUS_OFF)) != 0U) {

    status_set_flags(
        STATUS_FLAG_CAN_ERROR);

    } else {

        status_clear_flags(
            STATUS_FLAG_CAN_ERROR);
    }

}
