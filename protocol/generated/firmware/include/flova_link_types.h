/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 1
 */

#ifndef FLOVA_LINK_TYPES_H__
#define FLOVA_LINK_TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zcbor_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Which value for --default-max-qty this file was created with.
 *
 *  The define is used in the other generated file to do a build-time
 *  compatibility check.
 *
 *  See `zcbor --help` for more information about --default-max-qty
 */
#define DEFAULT_MAX_QTY 1

struct auth {
	struct zcbor_string auth_device_id;
	struct zcbor_string auth_secret;
};

struct auth_ok {
	uint64_t auth_ok_auth_server_utc_ms;
	uint64_t auth_ok_auth_heartbeat_ms;
	uint64_t auth_ok_auth_connection_generation;
};

struct ota_reported {
	struct zcbor_string ota_reported_reported_ota_id;
	uint64_t ota_reported_reported_ota_status;
	struct zcbor_string ota_reported_reported_ota_error;
	uint64_t ota_reported_reported_ota_progress;
};

struct time_request {
	uint64_t time_request_id;
	uint64_t time_request_monotonic_ms;
};

struct ota_desired_ota_target {
	struct zcbor_string ota_desired_ota_target;
};

struct ota_desired {
	struct zcbor_string ota_desired_ota_install_id;
	struct zcbor_string ota_desired_ota_version;
	struct zcbor_string ota_desired_ota_url;
	struct zcbor_string ota_desired_ota_checksum;
	uint64_t ota_desired_ota_size;
	struct ota_desired_ota_target ota_desired_ota_target;
	bool ota_desired_ota_target_present;
};

struct time_response {
	uint64_t time_response_request_id;
	uint64_t time_response_server_utc_ms;
};

struct flow_control {
	uint64_t flow_control_flow_retry_after_ms;
	struct zcbor_string flow_control_flow_reason;
	uint64_t flow_control_flow_capacity;
};

struct bootstrap_committed {
	struct zcbor_string bootstrap_committed_committed_device_id;
	uint64_t bootstrap_committed_committed_generation;
	uint64_t bootstrap_committed_committed_server_utc_ms;
};

struct heartbeat {
	uint64_t heartbeat_generation;
	uint64_t heartbeat_uptime_ms;
	uint64_t heartbeat_status;
	struct zcbor_string heartbeat_firmware_version;
};

struct config_reported {
	uint64_t config_reported_reported_config_generation;
	uint64_t config_reported_reported_config_status;
	struct zcbor_string config_reported_reported_config_checksum;
};

struct schedule_reported {
	uint64_t schedule_reported_reported_schedule_generation;
	uint64_t schedule_reported_reported_schedule_revision;
	uint64_t schedule_reported_reported_schedule_status;
	struct zcbor_string schedule_reported_reported_schedule_checksum;
};

struct schedule_renew {
	uint64_t schedule_renew_renew_generation;
	uint64_t schedule_renew_renew_revision;
};

struct config_ack {
	uint64_t config_ack_ack_generation;
	uint64_t config_ack_ack_sequence;
	uint64_t config_ack_ack_status;
	struct zcbor_string config_ack_ack_reason;
};

struct typed_value_fields_r {
	union {
		struct {
			bool typed_value_fields_value_bool_type_l_value_bool;
		};
		struct {
			int64_t typed_value_fields_value_int_type_l_value_int;
		};
		struct {
			float typed_value_fields_value_f32_type_l_value_f32;
		};
		struct {
			double typed_value_fields_value_f64_type_l_value_f64;
		};
		struct {
			struct zcbor_string typed_value_fields_value_text_type_l_value_text;
		};
	};
	enum {
		typed_value_fields_value_bool_type_l_c = 0,
		typed_value_fields_value_int_type_l_c = 1,
		typed_value_fields_value_f32_type_l_c = 2,
		typed_value_fields_value_f64_type_l_c = 3,
		typed_value_fields_value_text_type_l_c = 4,
	} typed_value_fields_choice;
};

struct correlation_id_r {
	union {
		struct zcbor_string correlation_id_empty_id_m;
		struct zcbor_string correlation_id_uuid_m;
	};
	enum {
		correlation_id_empty_id_m_c,
		correlation_id_uuid_m_c,
	} correlation_id_choice;
};

struct command {
	uint64_t command_generation;
	struct zcbor_string command_id;
	uint64_t command_compact_id;
	struct typed_value_fields_r command_typed_value_fields_m;
	uint64_t command_desired_version;
	uint64_t command_expires_at_utc_ms;
	struct correlation_id_r command_correlation_id;
};

struct config_desired {
	uint64_t config_desired_desired_config_generation;
	struct zcbor_string config_desired_desired_config_checksum;
	uint64_t config_desired_desired_config_record_count;
};

struct schedule_desired {
	uint64_t schedule_desired_desired_schedule_generation;
	uint64_t schedule_desired_desired_schedule_revision;
	uint64_t schedule_desired_desired_schedule_record_count;
	struct zcbor_string schedule_desired_desired_schedule_checksum;
};

struct config_end {
	uint64_t config_end_end_generation;
	uint64_t config_end_end_record_count;
	struct zcbor_string config_end_end_checksum;
};

struct schedule_end {
	uint64_t schedule_end_generation;
	uint64_t schedule_end_record_count;
	struct zcbor_string schedule_end_checksum;
};

struct capabilities {
	uint64_t capabilities_datastream_slots;
	uint64_t capabilities_input_slots;
	uint64_t capabilities_output_slots;
	uint64_t capabilities_command_slots;
	uint64_t capabilities_schedule_slots;
	uint64_t capabilities_manifest_bytes;
	uint64_t capabilities_history_bytes;
};

struct bootstrap_auth {
	struct zcbor_string bootstrap_auth_bootstrap_token;
	struct zcbor_string bootstrap_auth_bootstrap_secret;
	struct zcbor_string bootstrap_auth_hardware_id;
	struct zcbor_string bootstrap_auth_firmware_target;
	struct capabilities bootstrap_auth_bootstrap_capabilities;
};

struct command_result_ok {
	uint64_t command_result_ok_result_ok_generation;
	struct zcbor_string command_result_ok_result_ok_command_id;
	uint64_t command_result_ok_result_ok_compact_id;
	uint64_t command_result_ok_result_ok_status;
	struct typed_value_fields_r command_result_ok_typed_value_fields_m;
	uint64_t command_result_ok_result_ok_version;
	struct correlation_id_r command_result_ok_result_ok_correlation_id;
};

struct command_result_error {
	uint64_t command_result_error_result_error_generation;
	struct zcbor_string command_result_error_result_error_command_id;
	uint64_t command_result_error_result_error_compact_id;
	uint64_t command_result_error_result_error_status;
	struct zcbor_string command_result_error_result_error_code;
	struct zcbor_string command_result_error_result_error_message;
	uint64_t command_result_error_result_error_version;
	struct correlation_id_r command_result_error_result_error_correlation_id;
};

struct command_result_r {
	union {
		struct command_result_ok command_result_ok_m;
		struct command_result_error command_result_error_m;
	};
	enum {
		command_result_ok_m_c,
		command_result_error_m_c,
	} command_result_choice;
};

struct config_begin_config_command_id {
	struct zcbor_string config_begin_config_command_id;
};

struct config_begin {
	uint64_t config_begin_config_generation;
	struct zcbor_string config_begin_config_checksum;
	uint64_t config_begin_config_record_count;
	struct capabilities config_begin_negotiated_limits;
	struct zcbor_string config_begin_config_device_id;
	struct zcbor_string config_begin_config_template_id;
	struct zcbor_string config_begin_config_link_url;
	struct config_begin_config_command_id config_begin_config_command_id;
	bool config_begin_config_command_id_present;
};

struct state_reading {
	uint64_t state_reading_reading_compact_id;
	struct typed_value_fields_r state_reading_typed_value_fields_m;
	uint64_t state_reading_reading_revision;
};

struct state_readings {
	struct state_reading state_readings_state_reading_m[4];
	size_t state_readings_state_reading_m_count;
};

struct state {
	uint64_t state_generation;
	struct state_readings state_values;
};

struct schedule_action {
	uint64_t schedule_action_action_offset_ms;
	uint64_t schedule_action_action_compact_id;
	struct typed_value_fields_r schedule_action_typed_value_fields_m;
};

struct schedule_record {
	uint64_t schedule_record_schedule_id;
	bool schedule_record_schedule_enabled;
	uint64_t schedule_record_schedule_valid_from;
	uint64_t schedule_record_schedule_valid_until;
	struct schedule_action schedule_record_schedule_actions_schedule_action_m[8];
	size_t schedule_record_schedule_actions_schedule_action_m_count;
};

struct schedule_record_message {
	uint64_t schedule_record_message_schedule_message_generation;
	uint64_t schedule_record_message_schedule_message_sequence;
	struct schedule_record schedule_record_message_schedule_message_record;
};

struct datastream_record_datastream_minimum {
	struct typed_value_fields_r datastream_record_datastream_minimum;
};

struct datastream_record_datastream_maximum {
	struct typed_value_fields_r datastream_record_datastream_maximum;
};

struct datastream_record_datastream_default {
	struct typed_value_fields_r datastream_record_datastream_default;
};

struct hardware_mapping_mapping_active_high {
	bool hardware_mapping_mapping_active_high;
};

struct hardware_mapping_mapping_pull {
	uint64_t hardware_mapping_mapping_pull;
};

struct hardware_mapping_mapping_debounce_ms {
	uint64_t hardware_mapping_mapping_debounce_ms;
};

struct hardware_mapping_mapping_sample_ms {
	uint64_t hardware_mapping_mapping_sample_ms;
};

struct hardware_mapping_mapping_min_output_ms {
	uint64_t hardware_mapping_mapping_min_output_ms;
};

struct hardware_mapping {
	uint64_t hardware_mapping_mapping_kind;
	uint64_t hardware_mapping_mapping_pin;
	struct hardware_mapping_mapping_active_high hardware_mapping_mapping_active_high;
	bool hardware_mapping_mapping_active_high_present;
	struct hardware_mapping_mapping_pull hardware_mapping_mapping_pull;
	bool hardware_mapping_mapping_pull_present;
	struct hardware_mapping_mapping_debounce_ms hardware_mapping_mapping_debounce_ms;
	bool hardware_mapping_mapping_debounce_ms_present;
	struct hardware_mapping_mapping_sample_ms hardware_mapping_mapping_sample_ms;
	bool hardware_mapping_mapping_sample_ms_present;
	struct hardware_mapping_mapping_min_output_ms hardware_mapping_mapping_min_output_ms;
	bool hardware_mapping_mapping_min_output_ms_present;
};

struct datastream_record_datastream_mapping {
	struct hardware_mapping datastream_record_datastream_mapping;
};

struct datastream_record {
	uint64_t datastream_record_datastream_compact_id;
	struct zcbor_string datastream_record_datastream_uuid;
	struct zcbor_string datastream_record_datastream_key;
	uint64_t datastream_record_datastream_value_type;
	struct datastream_record_datastream_minimum datastream_record_datastream_minimum;
	bool datastream_record_datastream_minimum_present;
	struct datastream_record_datastream_maximum datastream_record_datastream_maximum;
	bool datastream_record_datastream_maximum_present;
	struct datastream_record_datastream_default datastream_record_datastream_default;
	bool datastream_record_datastream_default_present;
	struct datastream_record_datastream_mapping datastream_record_datastream_mapping;
	bool datastream_record_datastream_mapping_present;
};

struct system_record_system_heartbeat_ms {
	uint64_t system_record_system_heartbeat_ms;
};

struct system_record_system_status_led_pin {
	uint64_t system_record_system_status_led_pin;
};

struct system_record_system_status_led_active_low {
	bool system_record_system_status_led_active_low;
};

struct system_record_system_batch_flush_ms {
	uint64_t system_record_system_batch_flush_ms;
};

struct system_record {
	struct system_record_system_heartbeat_ms system_record_system_heartbeat_ms;
	bool system_record_system_heartbeat_ms_present;
	struct system_record_system_status_led_pin system_record_system_status_led_pin;
	bool system_record_system_status_led_pin_present;
	struct system_record_system_status_led_active_low system_record_system_status_led_active_low;
	bool system_record_system_status_led_active_low_present;
	struct system_record_system_batch_flush_ms system_record_system_batch_flush_ms;
	bool system_record_system_batch_flush_ms_present;
};

struct schedule_occurrence_record {
	uint64_t schedule_occurrence_record_occurrence_schedule_id;
	uint64_t schedule_occurrence_record_occurrence_chunk_index;
	uint64_t schedule_occurrence_record_occurrence_chunk_count;
	uint64_t schedule_occurrence_record_occurrence_values_uint64_m[16];
	size_t schedule_occurrence_record_occurrence_values_uint64_m_count;
};

struct safety_record_safety_minimum {
	struct typed_value_fields_r safety_record_safety_minimum;
};

struct safety_record_safety_maximum {
	struct typed_value_fields_r safety_record_safety_maximum;
};

struct safety_record_safety_timeout_ms {
	uint64_t safety_record_safety_timeout_ms;
};

struct safety_record {
	uint64_t safety_record_safety_compact_id;
	uint64_t safety_record_safety_policy;
	struct safety_record_safety_minimum safety_record_safety_minimum;
	bool safety_record_safety_minimum_present;
	struct safety_record_safety_maximum safety_record_safety_maximum;
	bool safety_record_safety_maximum_present;
	struct safety_record_safety_timeout_ms safety_record_safety_timeout_ms;
	bool safety_record_safety_timeout_ms_present;
};

struct config_record_body_r {
	union {
		struct datastream_record config_record_body_datastream_record_m;
		struct system_record config_record_body_system_record_m;
		struct schedule_record config_record_body_schedule_record_m;
		struct schedule_occurrence_record config_record_body_schedule_occurrence_record_m;
		struct safety_record config_record_body_safety_record_m;
	};
	enum {
		config_record_body_datastream_record_m_c,
		config_record_body_system_record_m_c,
		config_record_body_schedule_record_m_c,
		config_record_body_schedule_occurrence_record_m_c,
		config_record_body_safety_record_m_c,
	} config_record_body_choice;
};

struct config_record {
	uint64_t config_record_record_generation;
	uint64_t config_record_record_sequence;
	struct config_record_body_r config_record_record_body;
};

#ifdef __cplusplus
}
#endif

#endif /* FLOVA_LINK_TYPES_H__ */
