#include "test_code.h"

/* 内部Flash测试参数 */
#define INTERNAL_TEST_ADDR    0x0803E800UL   // 保留区首地址，可以改（内部Flash）

/* 外部Flash测试参数 */
#define TEST_START_ADDR     0x000000UL        // 测试起始地址（外部 Flash）
#define TEST_PAGES          10                // 写入页数（每页 256 字节）
#define TEST_SECTORS        4                 // 要擦除的扇区数（每扇区 4 KB），必须覆盖 TEST_PAGES * 256 字节


/* 内部Flash测试 */
void Test_InternalFlash(void)
{
    uint8_t write_buf[256];
    uint8_t read_buf[256];
    HAL_StatusTypeDef status;

    printf("=== Internal Flash Test ===\r\n");

    /* 1. 擦除测试页 */
    printf("Erasing page at 0x%08lX...\r\n", INTERNAL_TEST_ADDR);
    if (IntFlash_ErasePage(INTERNAL_TEST_ADDR) != HAL_OK)
    {
        printf("Erase FAILED!\r\n");
        return;
    }

    /* 2. 验证擦除：读回应全为 0xFF */
    memset(read_buf, 0, 256);
    // 直接读内存（因为内部 Flash 可直接访问）
    memcpy(read_buf, (void*)INTERNAL_TEST_ADDR, 256);
    for (int i = 0; i < 256; i++)
    {
        if (read_buf[i] != 0xFF)
        {
            printf("Erase verification FAILED at byte %d (0x%02X)\r\n", i, read_buf[i]);
            return;
        }
    }
    printf("Erase OK (all 0xFF).\r\n");

    /* 3. 准备测试数据 */
    for (int i = 0; i < 256; i++)
        write_buf[i] = (uint8_t)(i * 3 + 7);  // 任意非0xFF模式

    /* 4. 写入数据 */
    printf("Writing 256 bytes...\r\n");
    status = IntFlash_WriteBuffer(INTERNAL_TEST_ADDR, write_buf, 256);
    if (status != HAL_OK)
    {
        printf("Write FAILED (status=%d)\r\n", status);
        return;
    }

    /* 5. 读回并比对 */
    memset(read_buf, 0, 256);
    memcpy(read_buf, (void*)INTERNAL_TEST_ADDR, 256);
    if (memcmp(write_buf, read_buf, 256) == 0)
    {
        printf("Read back and compare PASSED!\r\n");
    }
    else
    {
        printf("Data mismatch!\r\n");
        // 打印第一个错误
        for (int i = 0; i < 256; i++)
        {
            if (write_buf[i] != read_buf[i])
            {
                printf("  Byte %d: expected 0x%02X, got 0x%02X\r\n", i, write_buf[i], read_buf[i]);
                break;
            }
        }
    }

    /* 6. 可选：擦除回来，恢复保留区原样（全0xFF） */
    IntFlash_ErasePage(INTERNAL_TEST_ADDR);
    printf("Test finished.\r\n");
}


/* 外部Flash测试 */
void Test_MultiPage_And_Erase(void)
{
    uint8_t write_buf[256];
    uint8_t read_buf[256];
    uint32_t addr;
    int page, i;
    HAL_StatusTypeDef status;

    printf("=== ZD25WQ32 Multi-Page & Erase Test ===\r\n");

    /* 1. 擦除足够的扇区（确保数据区全部擦除） */
    printf("Erasing %d sectors from 0x%08lX...\r\n", TEST_SECTORS, TEST_START_ADDR);
    for (i = 0; i < TEST_SECTORS; i++)
    {
        addr = TEST_START_ADDR + i * 4 * 1024;     // 每个扇区 4 KB
        if (ZD25WQ32_EraseSector(addr) != HAL_OK)
        {
            printf("Erase sector 0x%08lX FAILED!\r\n", addr);
            return;
        }
    }

    /* 可选：擦除后检查一个区域是否为 0xFF（证明擦除成功） */
    printf("Checking erased state...\r\n");
    ZD25WQ32_Read(TEST_START_ADDR, read_buf, 256);
    for (i = 0; i < 256; i++)
    {
        if (read_buf[i] != 0xFF)
        {
            printf("Erase verification FAILED at byte %d (expected 0xFF, got 0x%02X)\r\n", i, read_buf[i]);
            return;
        }
    }
    printf("Erase verification PASSED (all 0xFF).\r\n");

    /* 2. 写入多页测试数据 */
    printf("Writing %d pages...\r\n", TEST_PAGES);
    for (page = 0; page < TEST_PAGES; page++)
    {
        // 填充本页数据：例如每页数据 = 页号 + 偏移（或其他易于辨识的模式）
        for (i = 0; i < 256; i++)
        {
            write_buf[i] = (page * 256 + i) & 0xFF;   // 线性递增，跨页连续
        }

        addr = TEST_START_ADDR + page * 256;
        status = ZD25WQ32_WritePage(addr, write_buf, 256);
        if (status != HAL_OK)
        {
            printf("Write page %d at 0x%08lX FAILED!\r\n", page, addr);
            return;
        }
    }

    /* 3. 读回并逐页比较 */
    printf("Verifying written data...\r\n");
    for (page = 0; page < TEST_PAGES; page++)
    {
        addr = TEST_START_ADDR + page * 256;
        ZD25WQ32_Read(addr, read_buf, 256);

        // 生成期望数据
        for (i = 0; i < 256; i++)
        {
            write_buf[i] = (page * 256 + i) & 0xFF;
        }

        if (memcmp(write_buf, read_buf, 256) != 0)
        {
            printf("Data mismatch on page %d at 0x%08lX!\r\n", page, addr);
            // 可以打印第一个错误位置
            for (i = 0; i < 256; i++)
            {
                if (write_buf[i] != read_buf[i])
                {
                    printf("  Byte %d: expected 0x%02X, read 0x%02X\r\n", i, write_buf[i], read_buf[i]);
                    break;
                }
            }
            return;
        }
    }

    /* 4. 检查多页范围之外的擦除区域是否仍为 0xFF（确保未误写入） */
    printf("Checking untouched erased area...\r\n");
    addr = TEST_START_ADDR + TEST_PAGES * 256;   // 第一页未使用地址
    ZD25WQ32_Read(addr, read_buf, 64);           // 只读一小段即可
    for (i = 0; i < 64; i++)
    {
        if (read_buf[i] != 0xFF)
        {
            printf("Unexpected data outside written range at byte %d (0x%02X)!\r\n", i, read_buf[i]);
            return;
        }
    }

    printf("All tests PASSED! Multi-page write and sector erase verified.\r\n\r\n");
}
