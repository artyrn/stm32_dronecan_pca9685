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

#define CANARD_MEMORY_SIZE   1024U


static float actuator_value[ACTUATOR_COUNT];
static uint8_t actuator_valid[ACTUATOR_COUNT];
static uint32_t last_actuator_command_us = 0U;
static uint8_t have_actuator_command = 0U;

static uint32_t last_pca_recovery_us[PCA9685_DEVICE_COUNT];

static uint8_t pca_fault_active[PCA9685_DEVICE_COUNT];

static uint8_t actuator_failsafe_active = 0U;

static uint32_t actuator_rx_count = 0U;

uint32_t micros32(void);
uint64_t micros64(void);

static CanardInstance canard;
static uint8_t canard_memory[CANARD_MEMORY_SIZE];


static int actuator_to_pca(uint8_t actuator_id,
                           uint8_t *device,
                           uint8_t *channel)
{
    if ((actuator_id < 1U) ||
        (actuator_id > ACTUATOR_COUNT)) {
        return -1;
    }

    const uint8_t index =
        (uint8_t)(actuator_id - 1U);

    *device =
        (uint8_t)(
            index /
            PCA9685_CHANNELS_PER_DEVICE);

    *channel =
        (uint8_t)(
            index %
            PCA9685_CHANNELS_PER_DEVICE);

    return 0;
}


static uint16_t pca_status_flag(uint8_t device)
{
    return (device == 0U) ?
        STATUS_FLAG_PCA0_ERROR :
        STATUS_FLAG_PCA1_ERROR;
}


static void pca_recovery_process(void)
{
    const uint32_t now = micros32();

    for (uint8_t device = 0U;
         device < PCA9685_DEVICE_COUNT;
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

            uint16_t pwm_us =
                ACTUATOR_SAFE_PWM_US;

            if ((actuator_failsafe_active == 0U) &&
                (actuator_valid[actuator] != 0U)) {

                float pwm =
                    actuator_value[actuator];

                if (pwm < 500.0F) {
                    pwm = 500.0F;
                }

                if (pwm > 2500.0F) {
                    pwm = 2500.0F;
                }

                pwm_us = (uint16_t)pwm;
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

    const uint8_t timed_out =
        (have_actuator_command == 0U) ||
        ((uint32_t)(now - last_actuator_command_us) >=
         ACTUATOR_FAILSAFE_US);

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

    for (uint8_t actuator = 0U;
         actuator < ACTUATOR_COUNT;
         actuator++) {

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
                ACTUATOR_SAFE_PWM_US) < 0) {

            pca_fault_active[device] = 1U;

            status_set_flags(
                pca_status_flag(device));
        }

        /*
         * Do not pretend that safe PWM came
         * from DroneCAN.
         */
        actuator_valid[actuator] = 0U;

        actuator_value[actuator] =
            (float)ACTUATOR_SAFE_PWM_US;
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

        if ((cmd->actuator_id < 1U) ||
            (cmd->actuator_id > ACTUATOR_COUNT)) {
            continue;
        }

        uint8_t device;
        uint8_t channel;

        if (actuator_to_pca(
            cmd->actuator_id,
            &device,
            &channel) < 0) {

            continue;
        }
        const uint8_t actuator =
            (uint8_t)(cmd->actuator_id - 1U);

        if (cmd->command_type ==
            UAVCAN_EQUIPMENT_ACTUATOR_COMMAND_COMMAND_TYPE_PWM) {

            actuator_value[actuator] =
            cmd->command_value;

            actuator_valid[actuator] = 1U;

            have_actuator_command = 1U;
            last_actuator_command_us =
                micros32();

            actuator_rx_count++;

            float pwm = cmd->command_value;

            if (pwm < 500.0F) {
                pwm = 500.0F;
            }

            if (pwm > 2500.0F) {
                pwm = 2500.0F;
            }

            const int pca_result =
                pca9685_set_pwm_us(
                    device,
                    channel,
                    (uint16_t)pwm);

            if (pca_result < 0) {

                pca_fault_active[device] = 1U;

                status_set_flags(
                    pca_status_flag(device));

            }
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

    pca_fault_active[0U] =
        ((flags & STATUS_FLAG_PCA0_ERROR) != 0U)
            ? 1U
            : 0U;

    pca_fault_active[1U] =
        ((flags & STATUS_FLAG_PCA1_ERROR) != 0U)
            ? 1U
            : 0U;

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
}

void dronecan_actuator_process(void)
{
    actuator_failsafe_process();
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

    canardSetLocalNodeID(&canard, DRONECAN_NODE_ID);

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
