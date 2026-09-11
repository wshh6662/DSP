/*
 * Host-side protocol tests for modbus_tcp_server.c.
 *
 * Build/run with tests\run_modbus_tcp_tests.ps1. The module under test does not
 * include any device headers, so the whole Modbus TCP layer can be exercised on
 * a PC before anything is flashed to the F28384D.
 *
 * Tests are stateful on purpose: the server keeps one register map for the life
 * of the process, exactly like the DSP keeps one map for the life of the image.
 * Write/read-back pairs therefore have to run in the order listed in main().
 */

#include "modbus_tcp_server.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Value the application layer publishes at 4x-5 / 4x-6. */
#define TEST_DETECTION_COUNT 1234U

/* Expected contents of modbus_tcp_result_t after a request is processed. */
typedef struct {
    uint8_t  function;
    uint8_t  exception_code;
    uint16_t write_address;
    uint16_t write_quantity;
} expected_result_t;

static int failures = 0;

static void dump(const char *label, const uint8_t *bytes, uint16_t length)
{
    uint16_t index;

    fprintf(stderr, "    %-8s:", label);
    for (index = 0U; index < length; index++) {
        fprintf(stderr, " %02X", (unsigned)bytes[index]);
    }
    fprintf(stderr, "\n");
}

static void fail(const char *name, const char *what)
{
    fprintf(stderr, "FAIL  %s: %s\n", name, what);
    failures++;
}

static void run_request(const uint8_t *request,
                        uint16_t request_length,
                        uint8_t *response,
                        uint16_t *response_length,
                        modbus_tcp_result_t *result)
{
    memset(response, 0xA5, MODBUS_TCP_MAX_ADU_LENGTH);
    *response_length = ModbusTcp_ProcessRequest(request,
                                                request_length,
                                                TEST_DETECTION_COUNT,
                                                response,
                                                MODBUS_TCP_MAX_ADU_LENGTH,
                                                result);
}

/* Full response comparison, plus optional checks on the diagnostic result. */
static void expect(const char *name,
                   const uint8_t *request,
                   uint16_t request_length,
                   const uint8_t *expected,
                   uint16_t expected_length,
                   const expected_result_t *expected_result)
{
    uint8_t  response[MODBUS_TCP_MAX_ADU_LENGTH];
    modbus_tcp_result_t result;
    uint16_t actual_length;

    run_request(request, request_length, response, &actual_length, &result);

    if (actual_length != expected_length) {
        fprintf(stderr, "FAIL  %s: response length %u, expected %u\n",
                name, (unsigned)actual_length, (unsigned)expected_length);
        dump("actual", response, actual_length);
        dump("expected", expected, expected_length);
        failures++;
        return;
    }
    if (memcmp(response, expected, expected_length) != 0) {
        fail(name, "response bytes differ");
        dump("actual", response, actual_length);
        dump("expected", expected, expected_length);
        return;
    }
    if (expected_result != NULL) {
        if (result.function != expected_result->function) {
            fail(name, "g_modbus_last_function mismatch");
            return;
        }
        if (result.exception_code != expected_result->exception_code) {
            fail(name, "g_modbus_last_exception_code mismatch");
            return;
        }
        if (result.write_address != expected_result->write_address) {
            fail(name, "g_modbus_last_write_address mismatch");
            return;
        }
        if (result.write_quantity != expected_result->write_quantity) {
            fail(name, "g_modbus_last_write_quantity mismatch");
            return;
        }
    }
}

/* Frames that must be dropped entirely (return 0, no exception response). */
static void expect_dropped(const char *name,
                           const uint8_t *request,
                           uint16_t request_length)
{
    uint8_t  response[MODBUS_TCP_MAX_ADU_LENGTH];
    modbus_tcp_result_t result;
    uint16_t actual_length;

    run_request(request, request_length, response, &actual_length, &result);

    if (actual_length != 0U) {
        fprintf(stderr, "FAIL  %s: expected no response, got %u bytes\n",
                name, (unsigned)actual_length);
        dump("actual", response, actual_length);
        failures++;
    }
}

/* ---------------------------------------------------------------- reads -- */

/* A. 4x-5 / 4x-6 are a 32-bit value with the low word first. */
static void test_read_detection_count(void)
{
    static const uint8_t request[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x03, 0x00, 0x04, 0x00, 0x02
    };
    static const uint8_t expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x07,
        0x01, 0x03, 0x04, 0x04, 0xD2, 0x00, 0x00
    };
    static const expected_result_t result = { 0x03U, 0x00U, 0x00U, 0x00U };

    expect("A  FC03 read 4x-5 quantity 2 -> 1234 low word first",
           request, sizeof(request), expected, sizeof(expected), &result);
}

/* Untouched registers must read back as zero rather than garbage. */
static void test_unwritten_register_is_zero(void)
{
    static const uint8_t request[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x03, 0x00, 0x00, 0x00, 0x01
    };
    static const uint8_t expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x05,
        0x01, 0x03, 0x02, 0x00, 0x00
    };

    expect("   FC03 read 4x-1 (address 0) defaults to 0",
           request, sizeof(request), expected, sizeof(expected), NULL);
}

/* Detection count must be substituted wherever it lands inside a read window. */
static void test_detection_count_inside_window(void)
{
    static const uint8_t write_low[] = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x06, 0x00, 0x03, 0x11, 0x11
    };
    static const uint8_t write_high[] = {
        0x00, 0x02, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x06, 0x00, 0x06, 0x22, 0x22
    };
    static const uint8_t request[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x03, 0x00, 0x03, 0x00, 0x04
    };
    static const uint8_t expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x0B,
        0x01, 0x03, 0x08,
        0x11, 0x11,             /* address 3: ordinary register          */
        0x04, 0xD2,             /* address 4: detection count low word   */
        0x00, 0x00,             /* address 5: detection count high word  */
        0x22, 0x22              /* address 6: ordinary register          */
    };

    expect("   FC06 seed address 3",
           write_low, sizeof(write_low), write_low, sizeof(write_low), NULL);
    expect("   FC06 seed address 6",
           write_high, sizeof(write_high), write_high, sizeof(write_high), NULL);
    expect("   FC03 window spanning the detection count",
           request, sizeof(request), expected, sizeof(expected), NULL);
}

/* --------------------------------------------------------------- writes -- */

/* B. FC16 writing one holding register must be acknowledged, not ignored. */
static void test_write_single_register_with_fc16(void)
{
    static const uint8_t request[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x09,
        0x01, 0x10, 0x00, 0x0A, 0x00, 0x01, 0x02, 0x00, 0x7B
    };
    static const uint8_t expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x10, 0x00, 0x0A, 0x00, 0x01
    };
    static const expected_result_t result = { 0x10U, 0x00U, 0x000AU, 0x0001U };

    expect("B  FC16 write address 10 = 0x007B is echoed",
           request, sizeof(request), expected, sizeof(expected), &result);
}

/* C. The value written by B must be readable again. */
static void test_read_back_after_fc16(void)
{
    static const uint8_t request[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x03, 0x00, 0x0A, 0x00, 0x01
    };
    static const uint8_t expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x05,
        0x01, 0x03, 0x02, 0x00, 0x7B
    };

    expect("C  FC03 reads back address 10 = 0x007B",
           request, sizeof(request), expected, sizeof(expected), NULL);
}

/* D. FC06 write and echo. */
static void test_write_single_register_with_fc06(void)
{
    static const uint8_t request[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x06, 0x00, 0x0B, 0x12, 0x34
    };
    static const uint8_t expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x06, 0x00, 0x0B, 0x12, 0x34
    };
    static const uint8_t read_back_request[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x03, 0x00, 0x0B, 0x00, 0x01
    };
    static const uint8_t read_back_expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x05,
        0x01, 0x03, 0x02, 0x12, 0x34
    };
    static const expected_result_t result = { 0x06U, 0x00U, 0x000BU, 0x0001U };

    expect("D  FC06 write address 11 = 0x1234 is echoed",
           request, sizeof(request), expected, sizeof(expected), &result);
    expect("   FC03 reads back address 11 = 0x1234",
           read_back_request, sizeof(read_back_request),
           read_back_expected, sizeof(read_back_expected), NULL);
}

/* E. FC05 write single coil, FC01 read it back. */
static void test_single_coil_round_trip(void)
{
    static const uint8_t request[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x05, 0x00, 0x00, 0xFF, 0x00
    };
    static const uint8_t read_request[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x01, 0x00, 0x00, 0x00, 0x01
    };
    static const uint8_t read_set[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x04,
        0x01, 0x01, 0x01, 0x01
    };
    static const uint8_t clear_request[] = {
        0x12, 0x35, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x05, 0x00, 0x00, 0x00, 0x00
    };
    static const uint8_t read_clear[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x04,
        0x01, 0x01, 0x01, 0x00
    };
    static const expected_result_t set_result = { 0x05U, 0x00U, 0x0000U, 0x0001U };

    expect("E  FC05 set coil 0x-1",
           request, sizeof(request), request, sizeof(request), &set_result);
    expect("   FC01 reads coil 0x-1 = ON",
           read_request, sizeof(read_request), read_set, sizeof(read_set), NULL);
    expect("   FC05 clear coil 0x-1",
           clear_request, sizeof(clear_request),
           clear_request, sizeof(clear_request), NULL);
    expect("   FC01 reads coil 0x-1 = OFF",
           read_request, sizeof(read_request), read_clear, sizeof(read_clear), NULL);
}

/* F. FC0F write multiple coils, FC01 read them back with standard bit order. */
static void test_multiple_coils_round_trip(void)
{
    static const uint8_t request[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x09,
        0x01, 0x0F, 0x00, 0x00, 0x00, 0x0A, 0x02, 0xCD, 0x01
    };
    static const uint8_t expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x0F, 0x00, 0x00, 0x00, 0x0A
    };
    static const uint8_t read_request[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x01, 0x00, 0x00, 0x00, 0x0A
    };
    static const uint8_t read_expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x05,
        0x01, 0x01, 0x02, 0xCD, 0x01
    };
    static const expected_result_t result = { 0x0FU, 0x00U, 0x0000U, 0x000AU };

    expect("F  FC0F write 10 coils",
           request, sizeof(request), expected, sizeof(expected), &result);
    expect("   FC01 reads the 10 coils back",
           read_request, sizeof(read_request),
           read_expected, sizeof(read_expected), NULL);
}

/* Two coils in the same byte prove low-bit-first ordering. */
static void test_single_coil_bit_order(void)
{
    static const uint8_t clear_coil_0[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x05, 0x00, 0x00, 0x00, 0x00
    };
    static const uint8_t set_coil_1[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x05, 0x00, 0x01, 0xFF, 0x00
    };
    static const uint8_t clear_coil_1[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x05, 0x00, 0x01, 0x00, 0x00
    };
    static const uint8_t read_two_coils[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x01, 0x00, 0x00, 0x00, 0x02
    };
    static const uint8_t only_coil_1_on[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x04,
        0x01, 0x01, 0x01, 0x02
    };
    static const uint8_t both_off[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x04,
        0x01, 0x01, 0x01, 0x00
    };

    /* Start from a known state: the FC0F test above left coil 0 set. */
    expect("   FC05 clear coil 0x-1",
           clear_coil_0, sizeof(clear_coil_0),
           clear_coil_0, sizeof(clear_coil_0), NULL);
    expect("   FC05 set coil 0x-2",
           set_coil_1, sizeof(set_coil_1), set_coil_1, sizeof(set_coil_1), NULL);
    expect("   FC01 shows only bit 1 set (0x-1 = 0, 0x-2 = 1)",
           read_two_coils, sizeof(read_two_coils),
           only_coil_1_on, sizeof(only_coil_1_on), NULL);
    expect("   FC05 clear coil 0x-2",
           clear_coil_1, sizeof(clear_coil_1),
           clear_coil_1, sizeof(clear_coil_1), NULL);
    expect("   FC01 shows both coils clear",
           read_two_coils, sizeof(read_two_coils),
           both_off, sizeof(both_off), NULL);
}

/* --------------------------------------------------------- unsupported --- */

/* G. Format-valid but unsupported functions must answer with exception 01. */
static void test_unsupported_functions_answer_with_exception(void)
{
    static const uint8_t read_input_registers[] = {
        0x00, 0x07, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x04, 0x00, 0x00, 0x00, 0x01
    };
    static const uint8_t read_input_registers_expected[] = {
        0x00, 0x07, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x84, 0x01
    };
    static const uint8_t read_discrete_inputs[] = {
        0x00, 0x08, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x02, 0x00, 0x00, 0x00, 0x01
    };
    static const uint8_t read_discrete_inputs_expected[] = {
        0x00, 0x08, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x82, 0x01
    };
    static const uint8_t diagnostics[] = {
        0x00, 0x09, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x08, 0x00, 0x00, 0x00, 0x01
    };
    static const uint8_t diagnostics_expected[] = {
        0x00, 0x09, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x88, 0x01
    };
    static const expected_result_t illegal_function = { 0x04U, 0x01U, 0x00U, 0x00U };

    /* Nothing here may be silently dropped, and the socket must stay open. */
    expect("G  FC04 answers Illegal Function",
           read_input_registers, sizeof(read_input_registers),
           read_input_registers_expected, sizeof(read_input_registers_expected),
           &illegal_function);
    expect("G  FC02 answers Illegal Function",
           read_discrete_inputs, sizeof(read_discrete_inputs),
           read_discrete_inputs_expected, sizeof(read_discrete_inputs_expected),
           NULL);
    expect("G  FC08 answers Illegal Function",
           diagnostics, sizeof(diagnostics),
           diagnostics_expected, sizeof(diagnostics_expected), NULL);
}

/* ------------------------------------------------------------- errors ---- */

/* H. Range, quantity and byte-count validation per function code. */
static void test_address_range_errors(void)
{
    static const uint8_t read_at_the_limit[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x03, 0x08, 0x00, 0x00, 0x01
    };
    static const uint8_t read_past_the_limit[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x03, 0x07, 0xFF, 0x00, 0x02
    };
    static const uint8_t write_register_at_the_limit[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x06, 0x08, 0x00, 0x00, 0x01
    };
    static const uint8_t write_coil_at_the_limit[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x05, 0x08, 0x00, 0xFF, 0x00
    };
    static const uint8_t illegal_address_03[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x83, 0x02
    };
    static const uint8_t illegal_address_06[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x86, 0x02
    };
    static const uint8_t illegal_address_05[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x85, 0x02
    };

    expect("H  FC03 address 2048 is Illegal Data Address",
           read_at_the_limit, sizeof(read_at_the_limit),
           illegal_address_03, sizeof(illegal_address_03), NULL);
    expect("H  FC03 window 2047+2 is Illegal Data Address",
           read_past_the_limit, sizeof(read_past_the_limit),
           illegal_address_03, sizeof(illegal_address_03), NULL);
    expect("H  FC06 address 2048 is Illegal Data Address",
           write_register_at_the_limit, sizeof(write_register_at_the_limit),
           illegal_address_06, sizeof(illegal_address_06), NULL);
    expect("H  FC05 address 2048 is Illegal Data Address",
           write_coil_at_the_limit, sizeof(write_coil_at_the_limit),
           illegal_address_05, sizeof(illegal_address_05), NULL);
}

/* The detection count must not be overwritable by the HMI. */
static void test_detection_count_is_write_protected(void)
{
    static const uint8_t write_low[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x06, 0x00, 0x04, 0x00, 0x00
    };
    static const uint8_t write_high[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x06, 0x00, 0x05, 0x00, 0x00
    };
    static const uint8_t write_window[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x0D,
        0x01, 0x10, 0x00, 0x03, 0x00, 0x03, 0x06,
        0x00, 0x01, 0x00, 0x02, 0x00, 0x03
    };
    static const uint8_t illegal_address_06[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x86, 0x02
    };
    static const uint8_t illegal_address_10[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x90, 0x02
    };
    static const uint8_t read_detection[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x03, 0x00, 0x04, 0x00, 0x02
    };
    static const uint8_t read_detection_expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x07,
        0x01, 0x03, 0x04, 0x04, 0xD2, 0x00, 0x00
    };

    expect("H  FC06 write to 4x-5 is rejected",
           write_low, sizeof(write_low),
           illegal_address_06, sizeof(illegal_address_06), NULL);
    expect("H  FC06 write to 4x-6 is rejected",
           write_high, sizeof(write_high),
           illegal_address_06, sizeof(illegal_address_06), NULL);
    expect("H  FC10 window covering 4x-5 and 4x-6 is rejected",
           write_window, sizeof(write_window),
           illegal_address_10, sizeof(illegal_address_10), NULL);
    expect("   detection count still reads 1234",
           read_detection, sizeof(read_detection),
           read_detection_expected, sizeof(read_detection_expected), NULL);
}

static void test_quantity_errors(void)
{
    static const uint8_t read_zero[] = {
        0x00, 0x09, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x03, 0x00, 0x00, 0x00, 0x00
    };
    static const uint8_t read_too_many[] = {
        0x00, 0x09, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x03, 0x00, 0x00, 0x00, 0x7E
    };
    static const uint8_t coil_read_too_many[] = {
        0x00, 0x0B, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x01, 0x00, 0x00, 0x07, 0xD1
    };
    static const uint8_t illegal_value_03[] = {
        0x00, 0x09, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x83, 0x03
    };
    static const uint8_t illegal_value_01[] = {
        0x00, 0x0B, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x81, 0x03
    };

    expect("H  FC03 quantity 0 is Illegal Data Value",
           read_zero, sizeof(read_zero),
           illegal_value_03, sizeof(illegal_value_03), NULL);
    expect("H  FC03 quantity 126 exceeds the 125 limit",
           read_too_many, sizeof(read_too_many),
           illegal_value_03, sizeof(illegal_value_03), NULL);
    expect("H  FC01 quantity 2001 exceeds the 2000 limit",
           coil_read_too_many, sizeof(coil_read_too_many),
           illegal_value_01, sizeof(illegal_value_01), NULL);
}

static void test_coil_value_and_byte_count_errors(void)
{
    static const uint8_t bad_coil_value[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x05, 0x00, 0x00, 0x12, 0x34
    };
    static const uint8_t bad_coil_value_expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x85, 0x03
    };
    static const uint8_t fc10_bad_byte_count[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x0A,
        0x01, 0x10, 0x00, 0x0A, 0x00, 0x02, 0x03,
        0x00, 0x01, 0x02
    };
    static const uint8_t fc10_bad_byte_count_expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x90, 0x03
    };
    static const uint8_t fc10_zero_quantity[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x07,
        0x01, 0x10, 0x00, 0x0A, 0x00, 0x00, 0x00
    };
    static const uint8_t fc0f_bad_byte_count[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x08,
        0x01, 0x0F, 0x00, 0x00, 0x00, 0x0A, 0x01, 0xCD
    };
    static const uint8_t fc0f_bad_byte_count_expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x8F, 0x03
    };

    expect("H  FC05 value 0x1234 is Illegal Data Value",
           bad_coil_value, sizeof(bad_coil_value),
           bad_coil_value_expected, sizeof(bad_coil_value_expected), NULL);
    expect("H  FC10 byte count not twice the quantity",
           fc10_bad_byte_count, sizeof(fc10_bad_byte_count),
           fc10_bad_byte_count_expected, sizeof(fc10_bad_byte_count_expected), NULL);
    expect("H  FC10 quantity 0 is Illegal Data Value",
           fc10_zero_quantity, sizeof(fc10_zero_quantity),
           fc10_bad_byte_count_expected, sizeof(fc10_bad_byte_count_expected), NULL);
    expect("H  FC0F byte count does not match the quantity",
           fc0f_bad_byte_count, sizeof(fc0f_bad_byte_count),
           fc0f_bad_byte_count_expected, sizeof(fc0f_bad_byte_count_expected), NULL);
}

/* A per-function length check, instead of one global "mbap_length == 6". */
static void test_per_function_length_validation(void)
{
    static const uint8_t fc03_with_extra_payload[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x07,
        0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00
    };
    static const uint8_t fc03_with_extra_payload_expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x83, 0x03
    };
    static const uint8_t fc10_too_short[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x10, 0x00, 0x0A, 0x00, 0x01
    };
    static const uint8_t fc10_too_short_expected[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x03,
        0x01, 0x90, 0x03
    };

    expect("H  FC03 with MBAP length 7 is Illegal Data Value",
           fc03_with_extra_payload, sizeof(fc03_with_extra_payload),
           fc03_with_extra_payload_expected,
           sizeof(fc03_with_extra_payload_expected), NULL);
    expect("H  FC10 without a byte count is Illegal Data Value",
           fc10_too_short, sizeof(fc10_too_short),
           fc10_too_short_expected, sizeof(fc10_too_short_expected), NULL);
}

/* ---------------------------------------------------------- framing ------ */

static void test_framing_is_rejected(void)
{
    static const uint8_t incomplete[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x03, 0x00, 0x00
    };
    static const uint8_t wrapped_mbap_length[] = {
        0x12, 0x34, 0x00, 0x00, 0xFF, 0xFF
    };
    static const uint8_t bad_protocol_id[] = {
        0x12, 0x34, 0x00, 0x01, 0x00, 0x06,
        0x01, 0x03, 0x00, 0x00, 0x00, 0x01
    };
    static const uint8_t too_short[] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06, 0x01
    };

    expect_dropped("   incomplete FC03 frame is dropped", incomplete, sizeof(incomplete));
    expect_dropped("   wrapped MBAP length is dropped",
                   wrapped_mbap_length, sizeof(wrapped_mbap_length));
    expect_dropped("   non-zero protocol id is dropped",
                   bad_protocol_id, sizeof(bad_protocol_id));
    expect_dropped("   frame shorter than the MBAP header is dropped",
                   too_short, sizeof(too_short));
}

/* --------------------------------------------------------- boundaries ---- */

static void test_maximum_read_fits_the_adu(void)
{
    uint8_t request[12] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x03, 0x00, 0x00, 0x00, 125
    };
    uint8_t response[MODBUS_TCP_MAX_ADU_LENGTH];
    modbus_tcp_result_t result;
    uint16_t length;

    run_request(request, sizeof(request), response, &length, &result);

    /* 6-byte MBAP + Unit + FC + byte count + 250 data bytes = 259. */
    if (length != 259U) {
        fail("   FC03 read of 125 registers", "response length is not 259");
        return;
    }
    if (response[8] != 250U) {
        fail("   FC03 read of 125 registers", "byte count is not 250");
        return;
    }
    /* The detection count must still land on 4x-5 / 4x-6 inside a wide read.
       Payload starts at offset 9, so address 4 is at 9 + 4 * 2 = 17. */
    if ((response[17] != 0x04U) || (response[18] != 0xD2U) ||
        (response[19] != 0x00U) || (response[20] != 0x00U)) {
        fail("   FC03 read of 125 registers",
             "detection count missing at 4x-5 / 4x-6 in the payload");
        dump("actual", response, length);
        return;
    }
}

static void test_maximum_write_fits_the_adu(void)
{
    uint8_t request[259];
    uint8_t expected[12] = {
        0x12, 0x34, 0x00, 0x00, 0x00, 0x06,
        0x01, 0x10, 0x00, 0x64, 0x00, 123
    };
    uint16_t index;

    memset(request, 0, sizeof(request));
    request[0] = 0x12; request[1] = 0x34;
    request[4] = 0x00; request[5] = 0xFD;   /* MBAP length 253 */
    request[6] = 0x01;
    request[7] = 0x10;
    request[8] = 0x00; request[9] = 0x64;   /* address 100 */
    request[10] = 0x00; request[11] = 123;  /* 123 registers = 246 bytes */
    request[12] = 246;
    for (index = 13U; index < 259U; index++) {
        request[index] = (uint8_t)(index & 0xFFU);
    }

    expect("   FC10 write of 123 registers is acknowledged",
           request, sizeof(request), expected, sizeof(expected), NULL);
}

int main(void)
{
    test_unwritten_register_is_zero();
    test_read_detection_count();
    test_detection_count_inside_window();

    test_write_single_register_with_fc16();
    test_read_back_after_fc16();
    test_write_single_register_with_fc06();

    test_single_coil_round_trip();
    test_multiple_coils_round_trip();
    test_single_coil_bit_order();

    test_unsupported_functions_answer_with_exception();

    test_address_range_errors();
    test_detection_count_is_write_protected();
    test_quantity_errors();
    test_coil_value_and_byte_count_errors();
    test_per_function_length_validation();
    test_framing_is_rejected();

    test_maximum_read_fits_the_adu();
    test_maximum_write_fits_the_adu();

    if (failures != 0) {
        fprintf(stderr, "%d Modbus TCP protocol test(s) failed.\n", failures);
        return 1;
    }

    puts("Modbus TCP protocol tests passed.");
    return 0;
}
