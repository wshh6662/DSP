#include "modbus_tcp_server.h"

// ---------------------------------------------------------------- 帧布局 --
// 下列偏移均相对 ADU 首字节。MBAP 头固定 6 字节，其后是 Unit ID 和 PDU。
#define MODBUS_MBAP_TRANSACTION_HI      0U
#define MODBUS_MBAP_TRANSACTION_LO      1U
#define MODBUS_MBAP_PROTOCOL_HI         2U
#define MODBUS_MBAP_PROTOCOL_LO         3U
#define MODBUS_MBAP_LENGTH_HI           4U
#define MODBUS_MBAP_LENGTH_LO           5U
#define MODBUS_MBAP_UNIT_ID             6U
#define MODBUS_PDU_FUNCTION             7U
#define MODBUS_PDU_ADDRESS_HI           8U
#define MODBUS_PDU_ADDRESS_LO           9U
#define MODBUS_PDU_VALUE_HI            10U
#define MODBUS_PDU_VALUE_LO            11U
#define MODBUS_PDU_BYTE_COUNT          12U
#define MODBUS_PDU_DATA                13U

// MBAP Length 字段计的是 Unit ID + PDU 的字节数，最小 2（Unit + 功能码）。
#define MODBUS_MBAP_HEADER_LENGTH       6U
#define MODBUS_MBAP_MIN_FRAME_LENGTH    8U
#define MODBUS_SINGLE_REQUEST_LENGTH    6U
// 多写请求 = 单写请求 + 1 字节「字节数」字段。
#define MODBUS_MULTIPLE_REQUEST_BASE    7U
// 写响应固定回显 Unit + 功能码 + 地址 + 数量/取值。
#define MODBUS_WRITE_RESPONSE_LENGTH   12U
#define MODBUS_EXCEPTION_RESPONSE_LENGTH 9U

// Modbus 应用层协议规定的数量上限。
#define MODBUS_MAX_READ_COILS        2000U
#define MODBUS_MAX_READ_REGISTERS     125U
#define MODBUS_MAX_WRITE_COILS       1968U
#define MODBUS_MAX_WRITE_REGISTERS    123U

#define MODBUS_COIL_VALUE_ON        0xFF00U
#define MODBUS_COIL_VALUE_OFF       0x0000U
#define MODBUS_EXCEPTION_FLAG         0x80U

// HMI 可见的寄存器映射。全部静态分配，不使用 malloc。
// 掉电即丢失，这是测试阶段的预期行为。
//
// 这两个数组单独放进 "modbus_data" 段，由链接脚本放到 RAMGS0：
// 合计 2048 + 256 = 0x900 words（C28x 上 char 与 uint16_t 都占 1 word），
// 放进默认的 .bss（RAMLS5，仅 0x800 words）会直接链接失败。
// 该段不在 C 运行时自动清零的范围内，所以必须靠 ModbusTcp_ResetStorage()
// 显式清零，main() 在启动网络之前调用一次。
//
// DATA_SECTION 只有 TI 编译器认；宿主的 gcc 协议测试也编译这个文件，
// 不套 #ifdef 会因为 -Werror=unknown-pragmas 直接编译失败。
#define MODBUS_COIL_BYTE_COUNT  ((uint16_t)((MODBUS_COIL_COUNT + 7U) / 8U))

#ifdef __TI_COMPILER_VERSION__
#pragma DATA_SECTION(modbus_holding_registers, "modbus_data")
#endif
static uint16_t modbus_holding_registers[MODBUS_HOLDING_REGISTER_COUNT];

#ifdef __TI_COMPILER_VERSION__
#pragma DATA_SECTION(modbus_coils, "modbus_data")
#endif
static modbus_octet_t modbus_coils[(MODBUS_COIL_COUNT + 7U) / 8U];

// 作用：把 HMI 可见的寄存器映射清零。
// 用法：main() 启动时、使用寄存器映射之前调用一次。
//       modbus_data 段被链接到 RAMGS0，不在 .bss 的自动清零范围内，不清就会
//       在首次上电时读到随机值。
//       这里刻意用显式循环而不是 memset：C28x 上 char 是 16 位，
//       memset 的「字节」语义与 modbus_octet_t 数组的实际存储单位对不上，
//       写出来的代码容易让人误判长度。
void ModbusTcp_ResetStorage(void)
{
    uint16_t index;

    for (index = 0U; index < (uint16_t)MODBUS_HOLDING_REGISTER_COUNT; index++)
    {
        modbus_holding_registers[index] = 0U;
    }

    for (index = 0U; index < MODBUS_COIL_BYTE_COUNT; index++)
    {
        modbus_coils[index] = 0U;
    }
}

// ------------------------------------------------------------ 字节序工具 --
// 显式经 uint32_t 中转：C28x 上 char/int 都是 16 位，直接左移高字节会溢出。
static uint16_t Modbus_ReadU16(const modbus_octet_t *bytes)
{
    uint32_t high = (uint32_t)bytes[0];
    uint32_t low  = (uint32_t)bytes[1];

    return (uint16_t)((high << 8U) | low);
}

static void Modbus_WriteU16(modbus_octet_t *bytes, uint16_t value)
{
    uint32_t wide = (uint32_t)value;

    bytes[0] = (modbus_octet_t)((wide >> 8U) & 0xFFU);
    bytes[1] = (modbus_octet_t)(wide & 0xFFU);
}

// ---------------------------------------------------------------- 应答头 --
// 作用：复制事务号与 Unit ID，并按参数填写 MBAP Length。
static void Modbus_BeginResponse(const modbus_octet_t *request,
                                 modbus_octet_t *response,
                                 uint16_t mbap_length)
{
    response[MODBUS_MBAP_TRANSACTION_HI] = request[MODBUS_MBAP_TRANSACTION_HI];
    response[MODBUS_MBAP_TRANSACTION_LO] = request[MODBUS_MBAP_TRANSACTION_LO];
    response[MODBUS_MBAP_PROTOCOL_HI] = 0U;
    response[MODBUS_MBAP_PROTOCOL_LO] = 0U;
    Modbus_WriteU16(&response[MODBUS_MBAP_LENGTH_HI], mbap_length);
    response[MODBUS_MBAP_UNIT_ID] = request[MODBUS_MBAP_UNIT_ID];
}

// 作用：生成 Modbus 异常响应，功能码置最高位，后跟异常码。
static uint16_t Modbus_Exception(const modbus_octet_t *request,
                                 modbus_octet_t function,
                                 modbus_octet_t exception_code,
                                 modbus_octet_t *response,
                                 uint16_t response_capacity,
                                 modbus_tcp_result_t *result)
{
    if (response_capacity < MODBUS_EXCEPTION_RESPONSE_LENGTH)
    {
        return 0U;
    }

    Modbus_BeginResponse(request, response, 3U);
    response[MODBUS_PDU_FUNCTION] =
        (modbus_octet_t)(function | MODBUS_EXCEPTION_FLAG);
    response[MODBUS_PDU_ADDRESS_HI] = exception_code;

    if (result != 0)
    {
        result->exception_code = exception_code;
    }

    return MODBUS_EXCEPTION_RESPONSE_LENGTH;
}

// 作用：按标准回显写请求的地址与数量/取值。
static uint16_t Modbus_EchoWriteRequest(const modbus_octet_t *request,
                                        modbus_octet_t *response,
                                        uint16_t response_capacity)
{
    if (response_capacity < MODBUS_WRITE_RESPONSE_LENGTH)
    {
        return 0U;
    }

    Modbus_BeginResponse(request, response, MODBUS_SINGLE_REQUEST_LENGTH);
    response[MODBUS_PDU_FUNCTION] = request[MODBUS_PDU_FUNCTION];
    response[MODBUS_PDU_ADDRESS_HI] = request[MODBUS_PDU_ADDRESS_HI];
    response[MODBUS_PDU_ADDRESS_LO] = request[MODBUS_PDU_ADDRESS_LO];
    response[MODBUS_PDU_VALUE_HI] = request[MODBUS_PDU_VALUE_HI];
    response[MODBUS_PDU_VALUE_LO] = request[MODBUS_PDU_VALUE_LO];

    return MODBUS_WRITE_RESPONSE_LENGTH;
}

// ------------------------------------------------------------ 寄存器访问 --
// 作用：读取一个保持寄存器。4x-5 / 4x-6 动态返回检测数量，不占用存储数组。
static uint16_t Modbus_HoldingValue(uint16_t address, uint32_t detection_count)
{
    if (address == MODBUS_DETECTION_LOW_ADDRESS)
    {
        return (uint16_t)(detection_count & 0xFFFFU);
    }
    if (address == MODBUS_DETECTION_HIGH_ADDRESS)
    {
        return (uint16_t)((detection_count >> 16U) & 0xFFFFU);
    }

    return modbus_holding_registers[address];
}

// 作用：判断地址是否属于检测数量发布的地址（4x-5 / 4x-6）。
// HMI 写这些地址时会被接受并回正常响应，但值不保存：读取路径根本不去查
// modbus_holding_registers，4x-5 / 4x-6 永远返回动态的 g_detection_count。
// 之所以不返回异常，是因为 EasyBuilder 按「地址整段间隔」成段读写，段内
// 只要包含 4x-5，整段请求都会带上这两个地址；返回异常会让同一段内其它
// 地址的合法写入一起失败。
static modbus_octet_t Modbus_IsReservedHoldingAddress(uint16_t address)
{
    if ((address == MODBUS_DETECTION_LOW_ADDRESS) ||
        (address == MODBUS_DETECTION_HIGH_ADDRESS))
    {
        return 1U;
    }

    return 0U;
}

static modbus_octet_t Modbus_GetCoil(uint16_t address)
{
    modbus_octet_t packed = modbus_coils[(uint16_t)(address >> 3U)];

    return (modbus_octet_t)((packed >> (address & 7U)) & 0x01U);
}

static void Modbus_SetCoil(uint16_t address, modbus_octet_t value)
{
    uint16_t byte_index = (uint16_t)(address >> 3U);
    modbus_octet_t mask = (modbus_octet_t)(1U << (address & 7U));

    if (value != 0U)
    {
        modbus_coils[byte_index] = (modbus_octet_t)(modbus_coils[byte_index] | mask);
    }
    else
    {
        modbus_coils[byte_index] =
            (modbus_octet_t)(modbus_coils[byte_index] & (modbus_octet_t)(~mask));
    }
}

void ModbusTcp_ClearCoil(uint16_t address)
{
    if (address < MODBUS_COIL_COUNT)
    {
        Modbus_SetCoil(address, 0U);
    }
}

// ------------------------------------------------------------ 功能码实现 --
// 作用：FC01 读取线圈，按标准 Modbus 位序打包：每字节低位对应低地址线圈。
static uint16_t Modbus_ReadCoils(const modbus_octet_t *request,
                                 uint16_t mbap_length,
                                 modbus_octet_t *response,
                                 uint16_t response_capacity,
                                 modbus_tcp_result_t *result)
{
    uint16_t start_address;
    uint16_t quantity;
    uint16_t byte_count;
    uint16_t index;
    uint16_t bit_index;
    uint32_t required_length;

    if (mbap_length != MODBUS_SINGLE_REQUEST_LENGTH)
    {
        return Modbus_Exception(request, MODBUS_FC_READ_COILS,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }

    start_address = Modbus_ReadU16(&request[MODBUS_PDU_ADDRESS_HI]);
    quantity = Modbus_ReadU16(&request[MODBUS_PDU_VALUE_HI]);

    if ((quantity < 1U) || (quantity > MODBUS_MAX_READ_COILS))
    {
        return Modbus_Exception(request, MODBUS_FC_READ_COILS,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }
    if (((uint32_t)start_address + (uint32_t)quantity) > (uint32_t)MODBUS_COIL_COUNT)
    {
        return Modbus_Exception(request, MODBUS_FC_READ_COILS,
                                MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
                                response, response_capacity, result);
    }

    byte_count = (uint16_t)((quantity + 7U) / 8U);
    required_length = (uint32_t)MODBUS_MBAP_HEADER_LENGTH + 3U + (uint32_t)byte_count;
    if (required_length > (uint32_t)response_capacity)
    {
        return 0U;
    }

    Modbus_BeginResponse(request, response, (uint16_t)(3U + byte_count));
    response[MODBUS_PDU_FUNCTION] = MODBUS_FC_READ_COILS;
    response[MODBUS_PDU_ADDRESS_HI] = (modbus_octet_t)byte_count;

    // 先清零，保证最后一个字节的填充位为 0。
    for (index = 0U; index < byte_count; index++)
    {
        response[MODBUS_PDU_ADDRESS_LO + index] = 0U;
    }

    for (index = 0U; index < quantity; index++)
    {
        if (Modbus_GetCoil((uint16_t)(start_address + index)) != 0U)
        {
            bit_index = (uint16_t)(index >> 3U);
            response[MODBUS_PDU_ADDRESS_LO + bit_index] = (modbus_octet_t)(
                response[MODBUS_PDU_ADDRESS_LO + bit_index] |
                (modbus_octet_t)(1U << (index & 7U)));
        }
    }

    return (uint16_t)required_length;
}

// 作用：FC03 读取保持寄存器，范围内所有地址逐个取值。
static uint16_t Modbus_ReadHoldingRegisters(const modbus_octet_t *request,
                                            uint16_t mbap_length,
                                            uint32_t detection_count,
                                            modbus_octet_t *response,
                                            uint16_t response_capacity,
                                            modbus_tcp_result_t *result)
{
    uint16_t start_address;
    uint16_t quantity;
    uint16_t byte_count;
    uint16_t index;
    uint32_t required_length;

    if (mbap_length != MODBUS_SINGLE_REQUEST_LENGTH)
    {
        return Modbus_Exception(request, MODBUS_FC_READ_HOLDING_REGISTERS,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }

    start_address = Modbus_ReadU16(&request[MODBUS_PDU_ADDRESS_HI]);
    quantity = Modbus_ReadU16(&request[MODBUS_PDU_VALUE_HI]);

    if ((quantity < 1U) || (quantity > MODBUS_MAX_READ_REGISTERS))
    {
        return Modbus_Exception(request, MODBUS_FC_READ_HOLDING_REGISTERS,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }
    if (((uint32_t)start_address + (uint32_t)quantity) >
        (uint32_t)MODBUS_HOLDING_REGISTER_COUNT)
    {
        return Modbus_Exception(request, MODBUS_FC_READ_HOLDING_REGISTERS,
                                MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
                                response, response_capacity, result);
    }

    byte_count = (uint16_t)(quantity * 2U);
    required_length = (uint32_t)MODBUS_MBAP_HEADER_LENGTH + 3U + (uint32_t)byte_count;
    if (required_length > (uint32_t)response_capacity)
    {
        return 0U;
    }

    Modbus_BeginResponse(request, response, (uint16_t)(3U + byte_count));
    response[MODBUS_PDU_FUNCTION] = MODBUS_FC_READ_HOLDING_REGISTERS;
    response[MODBUS_PDU_ADDRESS_HI] = (modbus_octet_t)byte_count;

    for (index = 0U; index < quantity; index++)
    {
        Modbus_WriteU16(&response[MODBUS_PDU_ADDRESS_LO + (index * 2U)],
                        Modbus_HoldingValue((uint16_t)(start_address + index),
                                            detection_count));
    }

    return (uint16_t)required_length;
}

// 作用：FC05 写单个线圈，合法取值仅 FF00 与 0000。
static uint16_t Modbus_WriteSingleCoil(const modbus_octet_t *request,
                                       uint16_t mbap_length,
                                       modbus_octet_t *response,
                                       uint16_t response_capacity,
                                       modbus_tcp_result_t *result)
{
    uint16_t address;
    uint16_t value;

    if (mbap_length != MODBUS_SINGLE_REQUEST_LENGTH)
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_SINGLE_COIL,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }

    address = Modbus_ReadU16(&request[MODBUS_PDU_ADDRESS_HI]);
    value = Modbus_ReadU16(&request[MODBUS_PDU_VALUE_HI]);

    if ((value != MODBUS_COIL_VALUE_ON) && (value != MODBUS_COIL_VALUE_OFF))
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_SINGLE_COIL,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }
    if (address >= MODBUS_COIL_COUNT)
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_SINGLE_COIL,
                                MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
                                response, response_capacity, result);
    }

    Modbus_SetCoil(address,
                   (modbus_octet_t)((value == MODBUS_COIL_VALUE_ON) ? 1U : 0U));

    if (result != 0)
    {
        result->write_address = address;
        result->write_quantity = 1U;
    }

    return Modbus_EchoWriteRequest(request, response, response_capacity);
}

// 作用：FC06 写单个保持寄存器，回显地址与取值。
static uint16_t Modbus_WriteSingleRegister(const modbus_octet_t *request,
                                           uint16_t mbap_length,
                                           modbus_octet_t *response,
                                           uint16_t response_capacity,
                                           modbus_tcp_result_t *result)
{
    uint16_t address;
    uint16_t value;

    if (mbap_length != MODBUS_SINGLE_REQUEST_LENGTH)
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_SINGLE_REGISTER,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }

    address = Modbus_ReadU16(&request[MODBUS_PDU_ADDRESS_HI]);
    value = Modbus_ReadU16(&request[MODBUS_PDU_VALUE_HI]);

    if (address >= MODBUS_HOLDING_REGISTER_COUNT)
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_SINGLE_REGISTER,
                                MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
                                response, response_capacity, result);
    }
    // 检测数量由 DSP 动态发布：写 4x-5 / 4x-6 正常回显但不保存，读取时仍返回
    // 动态值。返回异常会让 HMI 成段写时因为夹带这两个地址而整段失败。
    if (Modbus_IsReservedHoldingAddress(address) == 0U)
    {
        modbus_holding_registers[address] = value;
    }

    if (result != 0)
    {
        result->write_address = address;
        result->write_quantity = 1U;
    }

    return Modbus_EchoWriteRequest(request, response, response_capacity);
}

// 作用：FC0F 写多个线圈，位序与 FC01 一致。
static uint16_t Modbus_WriteMultipleCoils(const modbus_octet_t *request,
                                          uint16_t mbap_length,
                                          modbus_octet_t *response,
                                          uint16_t response_capacity,
                                          modbus_tcp_result_t *result)
{
    uint16_t address;
    uint16_t quantity;
    uint16_t byte_count;
    uint16_t index;
    modbus_octet_t packed;

    if (mbap_length < MODBUS_MULTIPLE_REQUEST_BASE)
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_MULTIPLE_COILS,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }

    address = Modbus_ReadU16(&request[MODBUS_PDU_ADDRESS_HI]);
    quantity = Modbus_ReadU16(&request[MODBUS_PDU_VALUE_HI]);
    byte_count = (uint16_t)request[MODBUS_PDU_BYTE_COUNT];

    if ((quantity < 1U) || (quantity > MODBUS_MAX_WRITE_COILS))
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_MULTIPLE_COILS,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }
    if (byte_count != (uint16_t)((quantity + 7U) / 8U))
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_MULTIPLE_COILS,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }
    if (mbap_length != (uint16_t)(MODBUS_MULTIPLE_REQUEST_BASE + byte_count))
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_MULTIPLE_COILS,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }
    if (((uint32_t)address + (uint32_t)quantity) > (uint32_t)MODBUS_COIL_COUNT)
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_MULTIPLE_COILS,
                                MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
                                response, response_capacity, result);
    }

    for (index = 0U; index < quantity; index++)
    {
        packed = request[MODBUS_PDU_DATA + (index >> 3U)];
        Modbus_SetCoil((uint16_t)(address + index),
                       (modbus_octet_t)((packed >> (index & 7U)) & 0x01U));
    }

    if (result != 0)
    {
        result->write_address = address;
        result->write_quantity = quantity;
    }

    return Modbus_EchoWriteRequest(request, response, response_capacity);
}

// 作用：FC10 写多个保持寄存器。范围覆盖 4x-5 / 4x-6 时跳过这两个地址，
// 其余地址照常写入并回正常响应。
static uint16_t Modbus_WriteMultipleRegisters(const modbus_octet_t *request,
                                              uint16_t mbap_length,
                                              modbus_octet_t *response,
                                              uint16_t response_capacity,
                                              modbus_tcp_result_t *result)
{
    uint16_t address;
    uint16_t quantity;
    uint16_t byte_count;
    uint16_t index;

    if (mbap_length < MODBUS_MULTIPLE_REQUEST_BASE)
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_MULTIPLE_REGISTERS,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }

    address = Modbus_ReadU16(&request[MODBUS_PDU_ADDRESS_HI]);
    quantity = Modbus_ReadU16(&request[MODBUS_PDU_VALUE_HI]);
    byte_count = (uint16_t)request[MODBUS_PDU_BYTE_COUNT];

    if ((quantity < 1U) || (quantity > MODBUS_MAX_WRITE_REGISTERS))
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_MULTIPLE_REGISTERS,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }
    if (byte_count != (uint16_t)(quantity * 2U))
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_MULTIPLE_REGISTERS,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }
    if (mbap_length != (uint16_t)(MODBUS_MULTIPLE_REQUEST_BASE + byte_count))
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_MULTIPLE_REGISTERS,
                                MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                response, response_capacity, result);
    }
    if (((uint32_t)address + (uint32_t)quantity) >
        (uint32_t)MODBUS_HOLDING_REGISTER_COUNT)
    {
        return Modbus_Exception(request, MODBUS_FC_WRITE_MULTIPLE_REGISTERS,
                                MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
                                response, response_capacity, result);
    }
    // 段内包含 4x-5 / 4x-6 时跳过这两个地址、其余照常写入，不返回异常：
    // EasyBuilder 按「地址整段间隔」成段写，段内夹带检测数量地址是常态，
    // 整段拒绝会让同一段内合法的参数写入一起失败。
    for (index = 0U; index < quantity; index++)
    {
        if (Modbus_IsReservedHoldingAddress((uint16_t)(address + index)) == 0U)
        {
            modbus_holding_registers[address + index] =
                Modbus_ReadU16(&request[MODBUS_PDU_DATA + (index * 2U)]);
        }
    }

    if (result != 0)
    {
        result->write_address = address;
        result->write_quantity = quantity;
    }

    return Modbus_EchoWriteRequest(request, response, response_capacity);
}

// ------------------------------------------------------------------ 入口 --
// 作用：校验 MBAP 并按功能码分发。返回 0 表示整帧丢弃。
uint16_t ModbusTcp_ProcessRequest(const modbus_octet_t *request,
                                  uint16_t request_length,
                                  uint32_t detection_count,
                                  modbus_octet_t *response,
                                  uint16_t response_capacity,
                                  modbus_tcp_result_t *result)
{
    uint16_t mbap_length;
    uint32_t adu_length;
    modbus_octet_t function;

    if (result != 0)
    {
        result->function = 0U;
        result->exception_code = 0U;
        result->write_address = 0U;
        result->write_quantity = 0U;
    }

    if ((request == 0) || (response == 0))
    {
        return 0U;
    }
    if ((request_length < MODBUS_MBAP_MIN_FRAME_LENGTH) ||
        (response_capacity < MODBUS_EXCEPTION_RESPONSE_LENGTH))
    {
        return 0U;
    }
    // Modbus TCP 的协议标识固定为 0，其它值说明这不是 Modbus 帧。
    if ((request[MODBUS_MBAP_PROTOCOL_HI] != 0U) ||
        (request[MODBUS_MBAP_PROTOCOL_LO] != 0U))
    {
        return 0U;
    }

    mbap_length = Modbus_ReadU16(&request[MODBUS_MBAP_LENGTH_HI]);
    adu_length = (uint32_t)mbap_length + (uint32_t)MODBUS_MBAP_HEADER_LENGTH;
    if (adu_length > (uint32_t)request_length)
    {
        // 帧还没收全，交给上层继续攒；上层只会在收全后调用，这里兜底。
        return 0U;
    }
    if (adu_length > (uint32_t)MODBUS_TCP_MAX_ADU_LENGTH)
    {
        return 0U;
    }

    function = request[MODBUS_PDU_FUNCTION];
    if (result != 0)
    {
        result->function = function;
    }

    switch (function)
    {
    case MODBUS_FC_READ_COILS:
        return Modbus_ReadCoils(request, mbap_length,
                                response, response_capacity, result);

    case MODBUS_FC_READ_HOLDING_REGISTERS:
        return Modbus_ReadHoldingRegisters(request, mbap_length, detection_count,
                                           response, response_capacity, result);

    case MODBUS_FC_WRITE_SINGLE_COIL:
        return Modbus_WriteSingleCoil(request, mbap_length,
                                      response, response_capacity, result);

    case MODBUS_FC_WRITE_SINGLE_REGISTER:
        return Modbus_WriteSingleRegister(request, mbap_length,
                                          response, response_capacity, result);

    case MODBUS_FC_WRITE_MULTIPLE_COILS:
        return Modbus_WriteMultipleCoils(request, mbap_length,
                                         response, response_capacity, result);

    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
        return Modbus_WriteMultipleRegisters(request, mbap_length,
                                             response, response_capacity, result);

    default:
        // 合法但本服务器不支持的功能码必须回异常，不能静默丢帧。
        return Modbus_Exception(request, function,
                                MODBUS_EXCEPTION_ILLEGAL_FUNCTION,
                                response, response_capacity, result);
    }
}
