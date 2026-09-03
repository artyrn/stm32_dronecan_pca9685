#include <stdint.h>
#include <stdbool.h>
#include "status.h"
#include "canard.h"
#include "canard_stm32.h"
#include "uavcan.protocol.NodeStatus.h"
#include "uavcan.protocol.GetNodeInfo_res.h"
#include "uavcan.equipment.actuator.ArrayCommand.h"

#define DRONECAN_NODE_ID     42U
#define CANARD_MEMORY_SIZE   1024U
#define ACTUATOR_COUNT 16U
#define ACTUATOR_FAILSAFE_US 300000U
#define ACTUATOR_SAFE_PWM_US 1500U
#define PCA_RECOVERY_INTERVAL_US 1000000U


static float actuator_value[ACTUATOR_COUNT];
static uint8_t actuator_valid[ACTUATOR_COUNT];
static uint32_t last_actuator_command_us = 0U;

static uint32_t last_pca_recovery_us = 0U;
static uint8_t pca_fault_active = 0U;

uint32_t micros32(void);
uint64_t micros64(void);

static CanardInstance canard;
static uint8_t canard_memory[CANARD_MEMORY_SIZE];

int pca9685_set_pwm_us(uint8_t channel, uint16_t pulse_us);
int pca9685_recover(void);


static void pca_recovery_process(void)
{
    if (pca_fault_active == 0U) {
        return;
    }

    const uint32_t now = micros32();

    if ((uint32_t)(now - last_pca_recovery_us) <
        PCA_RECOVERY_INTERVAL_US) {
        return;
    }

    last_pca_recovery_us = now;

    if (pca9685_recover() != 0) {
        return;
    }

    /*
     * PCA9685 successfully re-initialized.
     *
     * Restore outputs.
     */

    if (status_get() == STATUS_ACTUATOR_FAILSAFE) {

        /*
         * If actuator failsafe is active,
         * restore safe PWM on all channels.
         */
        for (uint8_t channel = 0U;
             channel < ACTUATOR_COUNT;
             channel++) {

            if (pca9685_set_pwm_us(
                    channel,
                    ACTUATOR_SAFE_PWM_US) < 0) {

                pca_fault_active = 1U;
                status_set(STATUS_PCA_ERROR);
                return;
            }
        }

    } else {

        /*
         * Normal mode:
         * restore last valid actuator values.
         */
        for (uint8_t channel = 0U;
             channel < ACTUATOR_COUNT;
             channel++) {

            if (actuator_valid[channel] == 0U) {
                continue;
            }

            float pwm = actuator_value[channel];

            if (pwm < 500.0F) {
                pwm = 500.0F;
            }

            if (pwm > 2500.0F) {
                pwm = 2500.0F;
            }

            if (pca9685_set_pwm_us(
                    channel,
                    (uint16_t)pwm) < 0) {

                pca_fault_active = 1U;
                status_set(STATUS_PCA_ERROR);
                return;
            }
        }
    }

    /*
     * Recovery and output restoration succeeded.
     */
    pca_fault_active = 0U;

    if (status_get() == STATUS_PCA_ERROR) {
        status_set(STATUS_OK);
    }
}


static void actuator_failsafe_process(void)
{
    static uint8_t failsafe_active = 0U;

    const uint32_t now = micros32();

    if ((uint32_t)(now - last_actuator_command_us) >=
        ACTUATOR_FAILSAFE_US) {

        if (failsafe_active == 0U) {
            for (uint8_t channel = 0U;
                 channel < ACTUATOR_COUNT;
                 channel++) {

                if (pca9685_set_pwm_us(
                     channel,
                     ACTUATOR_SAFE_PWM_US) < 0) {

                     pca_fault_active = 1U;
                     status_set(STATUS_PCA_ERROR);
                }
                
                actuator_value[channel] =
                    (float)ACTUATOR_SAFE_PWM_US;

                actuator_valid[channel] = 0U;
            }

            if (status_get() != STATUS_PCA_ERROR) {
                status_set(STATUS_ACTUATOR_FAILSAFE);
            }
            failsafe_active = 1U;
        }

        return;
    }

    if (status_get() == STATUS_ACTUATOR_FAILSAFE) {
        status_set(STATUS_OK);
    }

    failsafe_active = 0U;
}

static void send_get_node_info_response(CanardRxTransfer *transfer)
{
    struct uavcan_protocol_GetNodeInfoResponse response = {0};

    response.status.uptime_sec = micros32() / 1000000U;
    response.status.health = UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK;
    response.status.mode = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    response.status.sub_mode = 0U;
    response.status.vendor_specific_status_code = 0U;

    /*
     * Software version 1.0.
     * Пока VCS commit и image CRC не передаем.
     */
    response.software_version.major = 1U;
    response.software_version.minor = 0U;
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
    response.hardware_version.unique_id[2] = 'A';
    response.hardware_version.unique_id[3] = '1';

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

    static const char node_name[] = "com.artyrn.pca9685";

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

        const uint8_t channel =
            (uint8_t)(cmd->actuator_id - 1U);

        if (cmd->command_type ==
            UAVCAN_EQUIPMENT_ACTUATOR_COMMAND_COMMAND_TYPE_PWM) {

            actuator_value[channel] =
                cmd->command_value;

            actuator_valid[channel] = 1U;

            last_actuator_command_us =
                micros32();

            float pwm = cmd->command_value;

            if (pwm < 500.0F) {
                pwm = 500.0F;
            }

            if (pwm > 2500.0F) {
                pwm = 2500.0F;
            }

            const int pca_result =
                pca9685_set_pwm_us(
                    channel,
                    (uint16_t)pwm);

            if (pca_result < 0) {
                pca_fault_active = 1U;
                status_set(STATUS_PCA_ERROR);
            } 

        }
    }

    canardReleaseRxTransferPayload(
        &canard,
        transfer);
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
    status.health = UAVCAN_PROTOCOL_NODESTATUS_HEALTH_OK;
    status.mode = UAVCAN_PROTOCOL_NODESTATUS_MODE_OPERATIONAL;
    status.sub_mode = 0U;
    status.vendor_specific_status_code = 0U;

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

    const uint32_t now = micros32();

    if ((uint32_t)(now - last_status_us) >= 1000000U) {
        last_status_us = now;
        send_node_status();
    }

    for (;;) {
        const CanardCANFrame *frame = canardPeekTxQueue(&canard);

        if (frame == 0) {
            break;
        }

        const int16_t result = canardSTM32Transmit(frame);

        if (result > 0) {
            canardPopTxQueue(&canard);
        } else if (result < 0) {
            canardPopTxQueue(&canard);
        } else {
            break;
        }
    }

    CanardCANFrame rx_frame;

    for (;;) {
        const int16_t rx_result =
            canardSTM32Receive(&rx_frame);

        if (rx_result > 0) {
            const uint32_t rx_now = micros32();

            (void)canardHandleRxFrame(
                &canard,
                &rx_frame,
                (uint64_t)rx_now);

            continue;
        }

        break;
    }
    actuator_failsafe_process();
    pca_recovery_process();
}

