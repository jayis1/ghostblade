/**
 * @file frame.h
 * @brief GhostWisp companion protocol – transport-neutral frame definitions.
 *
 * Author: jayis1
 *
 * This header is the canonical C definition of the GhostWisp companion
 * protocol frame layout.  It must remain in agreement with the Python
 * reference codec in protocol/frame.py and protocol/schema.py.
 *
 * Frame layout (all fields little-endian):
 *
 *   Offset  Size  Field
 *   ------  ----  -----
 *        0     2  magic            0x4757 ("GW")
 *        2     1  protocol_version uint8
 *        3     1  message_type     uint8
 *        4     1  flags            uint8
 *        5     1  reserved         0x00
 *        6     2  request_id       uint16
 *        8     2  session_id       uint16
 *       10     2  pad              0x0000
 *       12     4  monotonic_ms     uint32
 *       16     2  payload_length   uint16
 *       18     N  payload
 *    18+N     2  crc16            CRC-16/CCITT-FALSE over bytes [0..18+N-1]
 *
 * Total minimum frame size: 20 bytes (zero-length payload).
 * Maximum default payload: GW_PAYLOAD_MAX_DEFAULT bytes.
 *
 * Safety note: receiving this header and constructing a Frame struct does
 * not constitute execution of any active-mode operation.  RF/NFC/HID
 * execution requires the action broker and physical-confirmation state
 * machine in the GhostWisp firmware.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------- */

#define GW_MAGIC                   0x4757u   /**< 'G','W' little-endian      */
#define GW_PROTOCOL_VERSION        1u
#define GW_HEADER_SIZE             18u       /**< bytes before payload_length */
#define GW_OVERHEAD                20u       /**< header + len field + crc    */
#define GW_PAYLOAD_MAX_DEFAULT     4096u

/* --------------------------------------------------------------------------
 * Message types
 * -------------------------------------------------------------------------- */

typedef enum {
    /* Discovery */
    GW_MSG_IDENTITY_REQ       = 0x01,
    GW_MSG_IDENTITY_RESP      = 0x02,
    GW_MSG_CAPABILITIES_REQ   = 0x03,
    GW_MSG_CAPABILITIES_RESP  = 0x04,
    GW_MSG_HEALTH_REQ         = 0x05,
    GW_MSG_HEALTH_RESP        = 0x06,

    /* Synchronization */
    GW_MSG_TIME_SYNC_REQ      = 0x10,
    GW_MSG_TIME_SYNC_RESP     = 0x11,
    GW_MSG_POLICY_SYNC_REQ    = 0x12,
    GW_MSG_POLICY_SYNC_RESP   = 0x13,
    GW_MSG_PROFILE_INDEX_REQ  = 0x14,
    GW_MSG_PROFILE_INDEX_RESP = 0x15,
    GW_MSG_CAPTURE_INDEX_REQ  = 0x16,
    GW_MSG_CAPTURE_INDEX_RESP = 0x17,
    GW_MSG_AUDIT_CURSOR_REQ   = 0x18,
    GW_MSG_AUDIT_CURSOR_RESP  = 0x19,

    /* Observation */
    GW_MSG_OBS_SUB_GHZ        = 0x20,
    GW_MSG_OBS_NFC            = 0x21,
    GW_MSG_OBS_IR             = 0x22,
    GW_MSG_OBS_BUS            = 0x23,
    GW_MSG_OBS_BUTTON         = 0x24,

    /* Transfer */
    GW_MSG_TRANSFER_CHUNK     = 0x30,
    GW_MSG_TRANSFER_ACK       = 0x31,
    GW_MSG_TRANSFER_ABORT     = 0x32,

    /* Control */
    GW_MSG_STATUS_PUSH        = 0x40,
    GW_MSG_START_RECEIVE      = 0x41,
    GW_MSG_STOP_RECEIVE       = 0x42,
    GW_MSG_CANCEL             = 0x43,

    /* Active action (dual-consent required) */
    GW_MSG_ARM_REQ            = 0x50,
    GW_MSG_ARM_RESP           = 0x51,
    GW_MSG_CONFIRM_PHYSICAL   = 0x52,
    GW_MSG_EXECUTE_START      = 0x53,
    GW_MSG_EXECUTE_RESULT     = 0x54,
    GW_MSG_ABORT              = 0x55,

    /* Firmware update */
    GW_MSG_UPDATE_OFFER       = 0x60,
    GW_MSG_UPDATE_CHUNK       = 0x61,
    GW_MSG_UPDATE_FINALIZE    = 0x62,
    GW_MSG_UPDATE_STATUS      = 0x63,

    /* Diagnostics */
    GW_MSG_DIAG_COUNTERS      = 0x70,
    GW_MSG_DIAG_RESET_CAUSE   = 0x71,

    /* Session management */
    GW_MSG_SESSION_HELLO      = 0xF0,
    GW_MSG_SESSION_BYE        = 0xF1,
    GW_MSG_SESSION_ERROR      = 0xFF,
} gw_message_type_t;

/* --------------------------------------------------------------------------
 * Frame flags
 * -------------------------------------------------------------------------- */

#define GW_FLAG_MORE_FRAGS   0x01u  /**< More fragments follow               */
#define GW_FLAG_IDEMPOTENT   0x02u  /**< Safe to retry without side effects  */
#define GW_FLAG_MANDATORY    0x04u  /**< Unknown type must hard-reject        */
#define GW_FLAG_RESPONSE     0x08u  /**< This frame is a response             */

/* --------------------------------------------------------------------------
 * Frame header (packed, little-endian)
 * -------------------------------------------------------------------------- */

#pragma pack(push, 1)
typedef struct {
    uint16_t magic;             /**< Must equal GW_MAGIC                     */
    uint8_t  protocol_version;  /**< Must equal GW_PROTOCOL_VERSION           */
    uint8_t  message_type;      /**< gw_message_type_t                        */
    uint8_t  flags;             /**< GW_FLAG_* bitmask                        */
    uint8_t  reserved;          /**< Must be 0x00                             */
    uint16_t request_id;        /**< Monotonically increasing per session     */
    uint16_t session_id;        /**< Non-guessable; rotated each pairing      */
    uint16_t pad;               /**< Reserved; must be 0x0000                 */
    uint32_t monotonic_ms;      /**< Sender monotonic time in milliseconds    */
    uint16_t payload_length;    /**< Bytes that follow this field             */
    /* uint8_t  payload[payload_length]; -- variable, follows immediately */
    /* uint16_t crc16; -- CRC-16/CCITT-FALSE of everything before CRC      */
} gw_frame_header_t;
#pragma pack(pop)

/* --------------------------------------------------------------------------
 * Capability and safety flags (SessionCapabilities payload)
 * -------------------------------------------------------------------------- */

#define GW_CAP_SUB_GHZ_RECEIVE   (1u << 0)
#define GW_CAP_SUB_GHZ_TRANSMIT  (1u << 1)  /**< dual-consent required       */
#define GW_CAP_NFC_INSPECT       (1u << 2)
#define GW_CAP_NFC_EMULATE       (1u << 3)  /**< dual-consent required       */
#define GW_CAP_IR_RECEIVE        (1u << 4)
#define GW_CAP_IR_TRANSMIT       (1u << 5)  /**< dual-consent required       */
#define GW_CAP_USB_SERIAL        (1u << 6)
#define GW_CAP_USB_HID           (1u << 7)  /**< dual-consent required       */
#define GW_CAP_BUS_PROBE         (1u << 8)
#define GW_CAP_FIRMWARE_UPDATE   (1u << 9)
#define GW_CAP_COMPANION_SYNC    (1u << 10)

/** Safety flags; GW_SAFETY_RECEIVE_ONLY | GW_SAFETY_RADIO_DISABLED is the
 *  power-on default.  Active modes are opt-in per session after pairing. */
#define GW_SAFETY_RECEIVE_ONLY   (1u << 0)  /**< default: 1 (on at power-on) */
#define GW_SAFETY_RADIO_DISABLED (1u << 1)  /**< default: 1 (on at power-on) */
#define GW_SAFETY_HID_DISABLED   (1u << 2)
#define GW_SAFETY_NFC_EMULATE_OFF (1u << 3)

/* --------------------------------------------------------------------------
 * Active-mode codes (ArmRequestPayload.mode)
 * -------------------------------------------------------------------------- */

#define GW_ACTIVE_SUB_GHZ_TX  0x01u
#define GW_ACTIVE_NFC_EMULATE  0x02u
#define GW_ACTIVE_IR_TX        0x03u
#define GW_ACTIVE_USB_HID      0x04u

/* --------------------------------------------------------------------------
 * Abort reasons
 * -------------------------------------------------------------------------- */

#define GW_ABORT_TIMEOUT          0x01u
#define GW_ABORT_USER_CANCEL      0x02u
#define GW_ABORT_POLICY_VIOLATION 0x03u
#define GW_ABORT_REMOTE_ABORT     0x04u
#define GW_ABORT_LINK_LOST        0x05u

/* --------------------------------------------------------------------------
 * Payload structs (packed, little-endian)
 * -------------------------------------------------------------------------- */

#define GW_DEVICE_ID_LEN   16u
#define GW_VERSION_STR_LEN 32u
#define GW_OPERATOR_ID_LEN 16u
#define GW_AUTH_SCOPE_LEN  64u
#define GW_NONCE_LEN       16u

#pragma pack(push, 1)

typedef struct {
    uint8_t  device_id[GW_DEVICE_ID_LEN];
    uint16_t hw_rev;
    uint16_t fw_version;
    uint8_t  fw_tag[GW_VERSION_STR_LEN];
} gw_identity_payload_t;

typedef struct {
    uint16_t capability_flags;
    uint16_t safety_flags;
} gw_capabilities_payload_t;

typedef struct {
    uint64_t utc_ms;
    uint32_t roundtrip_ms;
} gw_time_sync_payload_t;

typedef struct {
    uint8_t  region_code;
    uint8_t  policy_flags;
    uint16_t reserved;
    uint32_t policy_seq;
} gw_policy_sync_payload_t;

typedef struct {
    uint8_t  mode;
    uint8_t  reserved;
    uint16_t duration_ms;
    uint32_t expiry_deadline_ms;  /**< monotonic; auto-disarm if elapsed     */
    uint8_t  operator_id[GW_OPERATOR_ID_LEN];
    uint8_t  auth_scope[GW_AUTH_SCOPE_LEN];
    uint8_t  nonce[GW_NONCE_LEN];
} gw_arm_request_payload_t;

typedef struct {
    uint8_t  nonce[GW_NONCE_LEN];  /**< Must match the ARM_REQ nonce         */
    uint32_t confirm_monotonic_ms;
} gw_confirm_physical_payload_t;

typedef struct {
    uint8_t  nonce[GW_NONCE_LEN];  /**< nonce of the ARM_REQ being aborted   */
    uint8_t  reason;
} gw_abort_payload_t;

#pragma pack(pop)

/* --------------------------------------------------------------------------
 * Inline helpers
 * -------------------------------------------------------------------------- */

/** Return the expected total frame size for a given payload_length. */
static inline size_t gw_frame_total_size(uint16_t payload_length) {
    return (size_t)GW_OVERHEAD + payload_length;
}

#ifdef __cplusplus
}
#endif
