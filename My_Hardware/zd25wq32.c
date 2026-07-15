#include "zd25wq32.h"

/* ==================== 内部辅助宏 ==================== */
#define QSPI_TIMEOUT        3000            // HAL 操作超时时间
#define WAIT_BUSY_TIMEOUT   2000            // 等待空闲默认超时
#define MY_FLASH_PAGE_SIZE  256             // 页大小

/* 命令码 */
#define CMD_WRITE_ENABLE    0x06
#define CMD_READ_STATUS1    0x05
#define CMD_READ_ID         0x9F
#define CMD_SECTOR_ERASE    0x20            // 4 KB
#define CMD_BLOCK_ERASE64   0xD8            // 64 KB
#define CMD_PAGE_PROGRAM    0x02
#define CMD_READ_DATA       0x03

/* 如果你的 WP/HOLD 引脚不是 PA6/PA7，请修改下面的引脚和端口 */
#define WP_HOLD_GPIO_PORT   GPIOA
#define WP_HOLD_GPIO_PIN    (GPIO_PIN_6 | GPIO_PIN_7)

/* ==================== 硬件初始化 ==================== */
/**
 * @brief 拉高 WP(IO2) 和 HOLD(IO3) 引脚，保证 Flash 可写且不被暂停
 * @note  必须在 QSPI 外设初始化后调用，或与 CubeMX 生成的 GPIO 初始化不冲突
 */
void ZD25WQ32_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能 GPIO 时钟（已使能也不要紧） */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* 配置 WP / HOLD 引脚为推挽输出，默认高电平 */
    GPIO_InitStruct.Pin   = WP_HOLD_GPIO_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(WP_HOLD_GPIO_PORT, &GPIO_InitStruct);

    /* 输出高电平 */
    HAL_GPIO_WritePin(WP_HOLD_GPIO_PORT, WP_HOLD_GPIO_PIN, GPIO_PIN_SET);
}

/* ==================== 基础命令封装 ==================== */
/**
 * @brief 读 JEDEC ID（9Fh），返回 3 字节
 */
HAL_StatusTypeDef ZD25WQ32_ReadID(uint8_t *pID)
{
    QSPI_CommandTypeDef sCmd = {0};

    sCmd.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction       = CMD_READ_ID;
    sCmd.AddressMode       = QSPI_ADDRESS_NONE;
    sCmd.DataMode          = QSPI_DATA_1_LINE;
    sCmd.DummyCycles       = 0;
    sCmd.NbData            = 3;

    if (HAL_QSPI_Command(&hqspi, &sCmd, QSPI_TIMEOUT) != HAL_OK)
        return HAL_ERROR;
    return HAL_QSPI_Receive(&hqspi, pID, QSPI_TIMEOUT);
}

/**
 * @brief 发送写使能命令 (06h)
 */
HAL_StatusTypeDef ZD25WQ32_WriteEnable(void)
{
    QSPI_CommandTypeDef sCmd = {0};

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = CMD_WRITE_ENABLE;
    sCmd.AddressMode     = QSPI_ADDRESS_NONE;
    sCmd.DataMode        = QSPI_DATA_NONE;
    sCmd.DummyCycles     = 0;

    return HAL_QSPI_Command(&hqspi, &sCmd, QSPI_TIMEOUT);
}

/**
 * @brief 读取状态寄存器 1 (05h)
 */
static HAL_StatusTypeDef ReadStatusReg1(uint8_t *status)
{
    QSPI_CommandTypeDef sCmd = {0};

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = CMD_READ_STATUS1;
    sCmd.AddressMode     = QSPI_ADDRESS_NONE;
    sCmd.DataMode        = QSPI_DATA_1_LINE;
    sCmd.DummyCycles     = 0;
    sCmd.NbData          = 1;

    if (HAL_QSPI_Command(&hqspi, &sCmd, QSPI_TIMEOUT) != HAL_OK)
        return HAL_ERROR;
    return HAL_QSPI_Receive(&hqspi, status, QSPI_TIMEOUT);
}

/**
 * @brief 等待 Flash 空闲（BUSY 位清零）
 * @param timeout_ms 超时时间（毫秒）
 */
HAL_StatusTypeDef ZD25WQ32_WaitBusy(uint32_t timeout_ms)
{
    uint8_t status;
    uint32_t tickstart = HAL_GetTick();

    do {
        if (ReadStatusReg1(&status) != HAL_OK)
            return HAL_ERROR;

        if ((HAL_GetTick() - tickstart) > timeout_ms)
            return HAL_TIMEOUT;
    } while ((status & 0x01) != 0);   // BUSY 位

    return HAL_OK;
}

/* ==================== 擦除 / 编程 / 读取 ==================== */
/**
 * @brief 擦除一个 4 KB 扇区
 * @param address 扇区起始地址（需要对齐到扇区边界）
 */
HAL_StatusTypeDef ZD25WQ32_EraseSector(uint32_t address)
{
    QSPI_CommandTypeDef sCmd = {0};
    
    address &= ~(0xFFFU);
    
    /* 写使能 */
    if (ZD25WQ32_WriteEnable() != HAL_OK)
        return HAL_ERROR;

    /* 发送擦除命令 */
    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = CMD_SECTOR_ERASE;
    sCmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    sCmd.AddressSize     = QSPI_ADDRESS_24_BITS;
    sCmd.Address         = address;
    sCmd.DataMode        = QSPI_DATA_NONE;
    sCmd.DummyCycles     = 0;

    if (HAL_QSPI_Command(&hqspi, &sCmd, QSPI_TIMEOUT) != HAL_OK)
        return HAL_ERROR;

    /* 等待完成 */
    return ZD25WQ32_WaitBusy(WAIT_BUSY_TIMEOUT);
}

/**
 * @brief 擦除一个 64 KB 块
 * @param address 块起始地址（需要对齐到 64 KB 边界）
 */
HAL_StatusTypeDef ZD25WQ32_EraseBlock64(uint32_t address)
{
    QSPI_CommandTypeDef sCmd = {0};
    address &= ~(0xFFFFU);
    
    if (ZD25WQ32_WriteEnable() != HAL_OK)
        return HAL_ERROR;

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = CMD_BLOCK_ERASE64;
    sCmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    sCmd.AddressSize     = QSPI_ADDRESS_24_BITS;
    sCmd.Address         = address;
    sCmd.DataMode        = QSPI_DATA_NONE;
    sCmd.DummyCycles     = 0;

    if (HAL_QSPI_Command(&hqspi, &sCmd, QSPI_TIMEOUT) != HAL_OK)
        return HAL_ERROR;

    return ZD25WQ32_WaitBusy(WAIT_BUSY_TIMEOUT);
}

/**
 * @brief 页编程（0~256 字节），地址与长度不能跨页。将 pdata 数组中的数据存放在 zd25wq32 的 address 地址处
 * @param address 写入起始地址（需保证 len 不跨越 256 字节边界）
 * @param pData   数据指针
 * @param size    写入字节数 (1~256)
 */
HAL_StatusTypeDef ZD25WQ32_WritePage(uint32_t address, uint8_t *pData, uint16_t size)
{
    QSPI_CommandTypeDef sCmd = {0};

    if (pData == NULL)
        return HAL_ERROR;
    if (size == 0 || size > MY_FLASH_PAGE_SIZE)
        return HAL_ERROR;
    if (((address % MY_FLASH_PAGE_SIZE) + size) > MY_FLASH_PAGE_SIZE)
        return HAL_ERROR;
    if (ZD25WQ32_WriteEnable() != HAL_OK)
        return HAL_ERROR;

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = CMD_PAGE_PROGRAM;
    sCmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    sCmd.AddressSize     = QSPI_ADDRESS_24_BITS;
    sCmd.Address         = address;
    sCmd.DataMode        = QSPI_DATA_1_LINE;
    sCmd.DummyCycles     = 0;
    sCmd.NbData          = size;

    if (HAL_QSPI_Command(&hqspi, &sCmd, QSPI_TIMEOUT) != HAL_OK)
        return HAL_ERROR;
    if (HAL_QSPI_Transmit(&hqspi, pData, QSPI_TIMEOUT) != HAL_OK)
        return HAL_ERROR;

    return ZD25WQ32_WaitBusy(WAIT_BUSY_TIMEOUT);
}

/**
 * @brief 从 zd25wq32 的 address 地址处读取任意长度数据放到 pdata 数组中（标准读 0x03，最大地址 0x3FFFFF）
 * @param address 读取起始地址
 * @param pData   存放数据的缓冲区
 * @param size    读取字节数
 */
HAL_StatusTypeDef ZD25WQ32_Read(uint32_t address, uint8_t *pData, uint16_t size)
{
    QSPI_CommandTypeDef sCmd = {0};

    sCmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    sCmd.Instruction     = CMD_READ_DATA;
    sCmd.AddressMode     = QSPI_ADDRESS_1_LINE;
    sCmd.AddressSize     = QSPI_ADDRESS_24_BITS;
    sCmd.Address         = address;
    sCmd.DataMode        = QSPI_DATA_1_LINE;
    sCmd.DummyCycles     = 0;
    sCmd.NbData          = size;

    if (HAL_QSPI_Command(&hqspi, &sCmd, QSPI_TIMEOUT) != HAL_OK)
        return HAL_ERROR;
    return HAL_QSPI_Receive(&hqspi, pData, QSPI_TIMEOUT);
}
