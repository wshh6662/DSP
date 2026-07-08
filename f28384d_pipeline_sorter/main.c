//
// Included Files
//
#include "driverlib.h"
#include "device.h"

// ==========================================================
// 1. HARDWARE PIN DEFINITIONS (from handoff + demo)
// ==========================================================

// --- Sensor Input ---
#define SENSOR_GPIO         26U

// --- Output GPIOs K0..K19 ---
// K0..K15 = GPIO0..GPIO15
// K16..K19 = GPIO22..GPIO25
#define K_OUTPUT_COUNT      20U

// --- SPIA + W5500 ---
#define SPI_SIMO_GPIO       16U
#define SPI_SOMI_GPIO       17U
#define SPI_CLK_GPIO        18U
#define W5500_CS_GPIO       19U
#define W5500_RST_GPIO      94U

// --- SCI2 (RS485) ---
#define SCI2_TX_GPIO        54U
#define SCI2_RX_GPIO        55U

// --- SCI3 (RS232 / debug) ---
// NOTE: Handoff said GPIO74 for TX, but F28384D GPIO74 does not
// have SCIC_TX. Using GPIO72 (GPIO_72_SCIC_TX) instead.
#define SCI3_TX_GPIO        72U
#define SCI3_RX_GPIO        73U

// ==========================================================
// 2. GLOBAL VARIABLES
// ==========================================================

// Encoder position snapshot at last sensor trigger
volatile uint32_t g_sensor_pos = 0;
volatile uint32_t g_sensor_count = 0;

// ==========================================================
// 3. INTERRUPT SERVICE ROUTINES
// ==========================================================

//
// XINT1 ISR - Sensor trigger (falling edge)
//
__interrupt void sensor_isr(void)
{
    // Capture encoder position at trigger instant
    g_sensor_pos = EQEP_getPosition(EQEP2_BASE);
    g_sensor_count++;

    //
    // Clear interrupt ACK for GROUP1 (XINT1 is in INT1.x)
    //
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

// ==========================================================
// 4. HARDWARE INITIALIZATION
// ==========================================================

//
// GPIO26 + XINT1 sensor input
//
void InitSensorInput(void)
{
    // GPIO26: input, pull-up, 6-sample qualification (debounce)
    GPIO_setDirectionMode(SENSOR_GPIO, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(SENSOR_GPIO, GPIO_PIN_TYPE_PULLUP);
    GPIO_setQualificationMode(SENSOR_GPIO, GPIO_QUAL_6SAMPLE);
}

//
// K0..K19 output GPIOs (GPIO0..15, GPIO22..25)
//
void InitOutputGPIOs(void)
{
    uint16_t i;
    const uint32_t k_outputs[K_OUTPUT_COUNT] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        22, 23, 24, 25
    };

    for (i = 0; i < K_OUTPUT_COUNT; i++)
    {
        uint32_t pin = k_outputs[i];
        GPIO_setDirectionMode(pin, GPIO_DIR_MODE_OUT);
        GPIO_setPadConfig(pin, GPIO_PIN_TYPE_STD);
        GPIO_writePin(pin, 1);  // inactive high
    }
}

//
// SPIA + W5500 control pins
// SPIA: SIMO=GPIO16, SOMI=GPIO17, CLK=GPIO18
// W5500: CS=GPIO19, RST=GPIO94
//
void InitSPI_W5500(void)
{
    //
    // SPIA pinmux and pad config
    //
    GPIO_setPinConfig(GPIO_16_SPIA_SIMO);
    GPIO_setPadConfig(SPI_SIMO_GPIO, GPIO_PIN_TYPE_PULLUP);
    GPIO_setQualificationMode(SPI_SIMO_GPIO, GPIO_QUAL_ASYNC);

    GPIO_setPinConfig(GPIO_17_SPIA_SOMI);
    GPIO_setPadConfig(SPI_SOMI_GPIO, GPIO_PIN_TYPE_PULLUP);
    GPIO_setQualificationMode(SPI_SOMI_GPIO, GPIO_QUAL_ASYNC);

    GPIO_setPinConfig(GPIO_18_SPIA_CLK);
    GPIO_setPadConfig(SPI_CLK_GPIO, GPIO_PIN_TYPE_PULLUP);
    GPIO_setQualificationMode(SPI_CLK_GPIO, GPIO_QUAL_ASYNC);

    //
    // SPIA configuration
    // 50 Mbps, Controller mode, POL0PHA1, 8-bit
    //
    SPI_disableModule(SPIA_BASE);
    SPI_setConfig(SPIA_BASE, DEVICE_LSPCLK_FREQ, SPI_PROT_POL0PHA1,
                  SPI_MODE_CONTROLLER, 50000000U, 8);
    SPI_setPTESignalPolarity(SPIA_BASE, SPI_PTE_ACTIVE_LOW);
    SPI_disableFIFO(SPIA_BASE);
    SPI_disableLoopback(SPIA_BASE);
    SPI_setEmulationMode(SPIA_BASE, SPI_EMULATION_FREE_RUN);
    SPI_enableModule(SPIA_BASE);

    //
    // W5500 CS (GPIO19)
    //
    GPIO_setPinConfig(GPIO_19_GPIO19);
    GPIO_setPadConfig(W5500_CS_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(W5500_CS_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_writePin(W5500_CS_GPIO, 1);   // deselect

    //
    // W5500 RST (GPIO94)
    //
    GPIO_setPinConfig(GPIO_94_GPIO94);
    GPIO_setPadConfig(W5500_RST_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_setDirectionMode(W5500_RST_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_writePin(W5500_RST_GPIO, 0);  // hold in reset for now
}

//
// SCI2 (RS485): TX=GPIO54, RX=GPIO55
//
void InitSCI2(void)
{
    GPIO_setPinConfig(GPIO_54_SCIB_TX);
    GPIO_setPinConfig(GPIO_55_SCIB_RX);

    GPIO_setPadConfig(SCI2_TX_GPIO, GPIO_PIN_TYPE_PULLUP);
    GPIO_setQualificationMode(SCI2_TX_GPIO, GPIO_QUAL_ASYNC);

    GPIO_setPadConfig(SCI2_RX_GPIO, GPIO_PIN_TYPE_PULLUP);
    GPIO_setQualificationMode(SCI2_RX_GPIO, GPIO_QUAL_ASYNC);

    //
    // SCI2: 115200 baud, 8N1
    //
    SCI_setConfig(SCIB_BASE, DEVICE_LSPCLK_FREQ, 115200U,
                  (SCI_CONFIG_WLEN_8 | SCI_CONFIG_STOP_ONE | SCI_CONFIG_PAR_NONE));
    SCI_disableLoopback(SCIB_BASE);
}

//
// SCI3 (RS232 / debug): TX=GPIO74, RX=GPIO73
//
void InitSCI3(void)
{
    GPIO_setPinConfig(GPIO_72_SCIC_TX);
    GPIO_setPinConfig(GPIO_73_SCIC_RX);

    GPIO_setPadConfig(SCI3_TX_GPIO, GPIO_PIN_TYPE_PULLUP);
    GPIO_setQualificationMode(SCI3_TX_GPIO, GPIO_QUAL_ASYNC);

    GPIO_setPadConfig(SCI3_RX_GPIO, GPIO_PIN_TYPE_PULLUP);
    GPIO_setQualificationMode(SCI3_RX_GPIO, GPIO_QUAL_ASYNC);

    //
    // SCI3: 115200 baud, 8N1
    //
    SCI_setConfig(SCIC_BASE, DEVICE_LSPCLK_FREQ, 115200U,
                  (SCI_CONFIG_WLEN_8 | SCI_CONFIG_STOP_ONE | SCI_CONFIG_PAR_NONE));
    SCI_disableLoopback(SCIC_BASE);
}

//
// ADCA: ADCINA0, ADCINA1
// ADCB: ADCINB0, ADCINB1
// Configure single-ended, 12-bit, SYSCLK-driven
//
void InitADC(void)
{
    //
    // ADCA
    //
    ADC_setPrescaler(ADCA_BASE, ADC_CLK_DIV_4_0);
    ADC_setMode(ADCA_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(ADCA_BASE, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(ADCA_BASE);

    //
    // ADCB
    //
    ADC_setPrescaler(ADCB_BASE, ADC_CLK_DIV_4_0);
    ADC_setMode(ADCB_BASE, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(ADCB_BASE, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(ADCB_BASE);

    // Delay for ADC startup (1ms)
    DEVICE_DELAY_US(1000);
}

//
// XINT1 interrupt setup
//
void InitXINT1(void)
{
    Interrupt_register(INT_XINT1, &sensor_isr);
    GPIO_setInterruptPin(SENSOR_GPIO, GPIO_INT_XINT1);
    GPIO_setInterruptType(GPIO_INT_XINT1, GPIO_INT_TYPE_FALLING_EDGE);
    GPIO_enableInterrupt(GPIO_INT_XINT1);
    Interrupt_enable(INT_XINT1);
}

//
// Top-level hardware setup
//
void Setup_Hardware(void)
{
    EALLOW;

    //
    // GPIO init (unlocks ports, enables pullups)
    //
    Device_initGPIO();

    //
    // Peripheral initialization
    //
    InitSensorInput();
    InitOutputGPIOs();
    InitSPI_W5500();
    InitSCI2();
    InitSCI3();
    InitADC();

    //
    // EQEP2 (pinmux + module — SysConfig already handled GPIO assign,
    // but we do module init here for robustness)
    //
    GPIO_setPinConfig(GPIO_78_EQEP2_A);
    GPIO_setPinConfig(GPIO_79_EQEP2_B);
    GPIO_setPinConfig(GPIO_81_EQEP2_INDEX);

    EQEP_setDecoderConfig(EQEP2_BASE, (EQEP_CONFIG_2X_RESOLUTION |
                                       EQEP_CONFIG_QUADRATURE |
                                       EQEP_CONFIG_NO_SWAP |
                                       EQEP_CONFIG_IGATE_DISABLE));
    EQEP_setPositionCounterConfig(EQEP2_BASE, EQEP_POSITION_RESET_MAX_POS, 0xFFFFFFFF);
    EQEP_setPosition(EQEP2_BASE, 0);
    EQEP_enableModule(EQEP2_BASE);

    EDIS;
}

// ==========================================================
// 6. MAIN
// ==========================================================

void main(void)
{
    //
    // Device init: watchdog, clock PLL, peripheral clocks, GPIO unlock
    //
    Device_init();

#ifdef _FLASH
    Device_bootCM(BOOTMODE_BOOT_TO_FLASH_SECTOR0);
#else
    Device_bootCM(BOOTMODE_BOOT_TO_S0RAM);
#endif

    //
    // Configure all peripherals
    //
    Setup_Hardware();

    //
    // Enable interrupts
    //
    DINT;
    Interrupt_initModule();
    Interrupt_initVectorTable();
    InitXINT1();
    EINT;
    ERTM;

    //
    // Main loop
    //
    while (1)
    {
        //
        // Placeholder: application logic goes here
        //

        //
        // Small delay to keep loop frequency reasonable
        //
        DEVICE_DELAY_US(100);
    }
}
