$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$source = Get-Content (Join-Path $project 'empty_driverlib_main.c') -Raw
$block = [regex]::Match($source, '(?s)#define CAMERA_BUZZER_DURATION_TICKS.*?(?=void main\(void\))').Value
if (-not $block) { throw 'Missing camera result output control.' }
if ($source -notmatch 'camera_result_init\(\);\s*camera_result_output_init\(\);' -or
    $source -notmatch '(?s)photoelectric_tcp_server_poll\(.*camera_result_output_poll\(\);') {
    throw 'Missing output initialization or post-receive polling.'
}
if ($source -match 'g_output_test_') { throw 'Independent test override must be removed.' }
$mock = @'
#include <stdint.h>
#include <assert.h>
#include "camera_result.h"
#define GPIO_1_GPIO1 1U
#define GPIO_4_GPIO4 4U
#define GPIO_PIN_TYPE_OD 1U
#define GPIO_DIR_MODE_OUT 1U
static uint32_t pins[5], configured, directed, padded, writes;
static uint32_t now;
uint32_t turntable_speed_get_tick_count(void) { return now; }
void GPIO_setPinConfig(uint32_t pin) { assert(pin == 1U || pin == 4U); configured |= 1U << pin; }
void GPIO_writePin(uint32_t pin, uint32_t value) { assert(pin == 1U || pin == 4U); assert(value <= 1U); pins[pin] = value; writes++; }
void GPIO_setPadConfig(uint32_t pin, uint32_t mode) { assert(mode == GPIO_PIN_TYPE_OD); padded |= 1U << pin; }
void GPIO_setDirectionMode(uint32_t pin, uint32_t mode) { assert(mode == GPIO_DIR_MODE_OUT); assert(pins[pin] == 1U); directed |= 1U << pin; }
static void feed(const char *text) { while (*text) { camera_result_feed_byte((camera_result_octet_t)*text++); } }
'@
$test = @'
int main(void) {
    uint32_t previous_writes;
    camera_result_init(); camera_result_output_init();
    assert(configured == 18U && directed == 18U && padded == 18U);
    assert(pins[1] == 1U && pins[4] == 1U);
    previous_writes = writes;
    feed("12,tcmc1"); camera_result_output_poll();
    assert(writes == previous_writes);
    feed(","); camera_result_output_poll();
    assert(pins[1] == 1U && pins[4] == 0U);
    previous_writes = writes;
    camera_result_output_poll(); assert(writes == previous_writes);
    feed("tcmc2,"); camera_result_output_poll();
    assert(pins[1] == 0U && pins[4] == 1U);
    previous_writes = writes;
    feed("tcmc3,"); camera_result_output_poll(); assert(writes == previous_writes);
    feed("tcmc2,"); camera_result_output_poll(); assert(writes == previous_writes + 2U);
    previous_writes = writes;
    feed("tcmc1"); camera_result_reset_partial_frame(); feed(",");
    camera_result_output_poll(); assert(writes == previous_writes);
    assert(pins[1] == 0U && pins[4] == 1U);
    feed("tcmc1,"); camera_result_output_poll();
    assert(pins[1] == 0U && pins[4] == 0U); // GOOD must not cancel active BAD pulse.
    assert(g_camera_inspection_result_count == 4U);
    now = 499U; camera_result_output_poll(); assert(pins[1] == 0U);
    now = 500U; camera_result_output_poll(); assert(pins[1] == 1U);
    now = 1000U; feed("tcmc2,"); camera_result_output_poll();
    now = 1400U; feed("tcmc2,"); camera_result_output_poll();
    now = 1899U; camera_result_output_poll(); assert(pins[1] == 0U);
    now = 1900U; camera_result_output_poll(); assert(pins[1] == 1U);
    now = UINT32_MAX - 100U; feed("tcmc2,"); camera_result_output_poll();
    now = 398U; camera_result_output_poll(); assert(pins[1] == 0U);
    now = 399U; camera_result_output_poll(); assert(pins[1] == 1U);
    // BAD then GOOD in one TCP receive batch must still trigger the buzzer.
    now = 600U; feed("tcmc2,tcmc1,"); camera_result_output_poll();
    assert(pins[1] == 0U && pins[4] == 0U);
    now = 900U; camera_result_reset_partial_frame(); camera_result_output_poll();
    now = 1100U; camera_result_output_poll(); assert(pins[1] == 1U);
}
'@
$dir = Join-Path $env:TEMP 'f28384d_pipeline_sorter_tests'
New-Item -ItemType Directory -Force $dir | Out-Null
$generated = Join-Path $dir 'test_camera_result_output.c'
$exe = Join-Path $dir 'test_camera_result_output.exe'
[IO.File]::WriteAllText($generated, $mock + "`n" + $block + "`n" + $test, [Text.UTF8Encoding]::new($false))
& gcc -std=c99 -Wall -Wextra -Werror -I $project $generated (Join-Path $project 'camera_result.c') -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host 'Camera result K1/K4 integration tests passed (no physical voltage verification).'
