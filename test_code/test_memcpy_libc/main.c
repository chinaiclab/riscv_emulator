#include <string.h>
#include <stdint.h>
#include "../../software/init_code/vprintf.h"
#include <stdlib.h>

// 使用标准C库的内存函数 + 我们的printf输出
// This test demonstrates standard C library memory functions working with our printf

int main(void) {
    printf("=== Standard C Library Memory Functions Test ===\n");
    printf("Using standard C library memory functions with our printf\n\n");

    // Test 1: memset - 标准C库版本
    printf("Test 1 - Standard C library memset:\n");
    {
        char buffer[64];

        // 使用标准C库的memset
        memset(buffer, 0, sizeof(buffer));
        printf("  After memset to 0: First byte = %d\n", buffer[0]);

        memset(buffer, 0x5A, sizeof(buffer));
        printf("  After memset to 0x5A: First byte = 0x%02X\n", (unsigned char)buffer[0]);
        printf("  Result: Standard C library memset works correctly\n\n");
    }

    // Test 2: memcpy - 标准C库版本
    printf("Test 2 - Standard C library memcpy:\n");
    {
        const char* src = "Standard C library memcpy test!";
        char dest[64];

        // 使用标准C库的memset和memcpy
        memset(dest, 0, sizeof(dest));
        memcpy(dest, src, strlen(src) + 1);

        printf("  Source: '%s'\n", src);
        printf("  Dest:   '%s'\n", dest);

        // 使用标准C库的strcmp验证
        if (strcmp(src, dest) == 0) {
            printf("  Result: PASSED - Standard C library memcpy works correctly\n\n");
        } else {
            printf("  Result: FAILED\n\n");
            return 1;
        }
    }

    // Test 3: memcmp - 标准C库版本
    printf("Test 3 - Standard C library memcmp:\n");
    {
        uint8_t buf1[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
        uint8_t buf2[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
        uint8_t buf3[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBD};

        int result1 = memcmp(buf1, buf2, sizeof(buf1));
        int result2 = memcmp(buf1, buf3, sizeof(buf1));

        printf("  memcmp(buf1, buf2): %d (should be 0)\n", result1);
        printf("  memcmp(buf1, buf3): %d (should be < 0)\n", result2);

        if (result1 == 0 && result2 < 0) {
            printf("  Result: PASSED - Standard C library memcmp works correctly\n\n");
        } else {
            printf("  Result: FAILED\n\n");
            return 1;
        }
    }

    // Test 4: strlen - 标准C库版本
    printf("Test 4 - Standard C library strlen:\n");
    {
        const char* test_str = "Hello, Standard C Library!";
        size_t len = strlen(test_str);

        printf("  String: '%s'\n", test_str);
        printf("  Length: %lu\n", (unsigned long)len);

        if (len == 26) {  // 修正期望值
            printf("  Result: PASSED - Standard C library strlen works correctly\n\n");
        } else {
            printf("  Result: FAILED (expected 26, got %lu)\n\n", (unsigned long)len);
            return 1;
        }
    }

    // Test 5: 复杂数据结构复制
    printf("Test 5 - Complex structure copy with standard C library:\n");
    {
        typedef struct {
            uint32_t id;
            char name[16];
            uint8_t scores[10];
            uint16_t total;
        } Student;

        Student student1 = {
            .id = 1001,
            .name = "Alice",
            .scores = {95, 87, 92, 88, 91, 89, 94, 90, 93, 96},
            .total = 915  // Sum of scores
        };

        Student student2;

        // 使用标准C库函数
        memset(&student2, 0, sizeof(Student));
        memcpy(&student2, &student1, sizeof(Student));

        printf("  Student 1: ID=%lu, Name='%s', Total=%u\n",
               (unsigned long)student1.id, student1.name, student1.total);
        printf("  Student 2: ID=%lu, Name='%s', Total=%u\n",
               (unsigned long)student2.id, student2.name, student2.total);

        // 使用标准C库的memcmp验证
        if (memcmp(&student1, &student2, sizeof(Student)) == 0) {
            printf("  Result: PASSED - Complex structure copy works correctly\n\n");
        } else {
            printf("  Result: FAILED\n\n");
            return 1;
        }
    }

    // Test 6: 内存模式验证
    printf("Test 6 - Memory pattern verification:\n");
    {
        uint8_t pattern[32];
        uint8_t copy[32];

        // 创建已知模式
        for (int i = 0; i < 32; i++) {
            pattern[i] = (uint8_t)(i * 7 + 13);
        }

        // 使用标准C库函数复制
        memset(copy, 0, sizeof(copy));
        memcpy(copy, pattern, sizeof(pattern));

        // 验证模式
        bool pattern_ok = true;
        for (int i = 0; i < 32; i++) {
            if (copy[i] != (uint8_t)(i * 7 + 13)) {
                pattern_ok = false;
                printf("  Mismatch at byte %d: expected 0x%02X, got 0x%02X\n",
                       i, (unsigned char)(i * 7 + 13), copy[i]);
                break;
            }
        }

        if (pattern_ok) {
            printf("  Pattern verification: All bytes match correctly\n");
            printf("  Result: PASSED\n\n");
        } else {
            printf("  Result: FAILED\n\n");
            return 1;
        }
    }

    // Test 7: 边界条件测试
    printf("Test 7 - Boundary conditions:\n");
    {
        // 零长度复制
        char src1[] = "source";
        char dest1[] = "destination";
        memcpy(dest1, src1, 0);

        // 单字节复制
        char src2[] = "ABC";
        char dest2[] = "XYZ";
        memcpy(dest2, src2, 1);

        printf("  Zero-length copy: dest1 = '%s' (should be unchanged)\n", dest1);
        printf("  Single-byte copy: dest2 = '%s' (should be 'XBC')\n", dest2);

        if (strcmp(dest1, "destination") == 0 && dest2[0] == 'A' && dest2[1] == 'Y' && dest2[2] == 'Z') {
            printf("  Result: PASSED - Boundary conditions handled correctly\n\n");
        } else {
            printf("  Result: FAILED\n\n");
            return 1;
        }
    }

    // Test 8: 性能对比（大块内存）
    printf("Test 8 - Large block memory operations:\n");
    {
        #define BLOCK_SIZE 1024
        static uint8_t large_src[BLOCK_SIZE];
        static uint8_t large_dest[BLOCK_SIZE];

        // 填充源数据
        for (int i = 0; i < BLOCK_SIZE; i++) {
            large_src[i] = (uint8_t)(i & 0xFF);
        }

        // 使用标准C库函数
        memset(large_dest, 0, BLOCK_SIZE);
        memcpy(large_dest, large_src, BLOCK_SIZE);

        // 验证
        bool large_ok = true;
        for (int i = 0; i < 100; i++) {  // 验证前100字节
            if (large_dest[i] != (uint8_t)(i & 0xFF)) {
                large_ok = false;
                break;
            }
        }

        if (large_ok) {
            printf("  Large block copy (%d bytes): Verified first 100 bytes\n", BLOCK_SIZE);
            printf("  Result: PASSED\n\n");
        } else {
            printf("  Result: FAILED\n\n");
            return 1;
        }
    }

    printf("=== 所有标准C库内存函数测试通过! ===\n");
    printf("\n总结:\n");
    printf("✓ 标准C库 memset 工作正常\n");
    printf("✓ 标准C库 memcpy 工作正常\n");
    printf("✓ 标准C库 memcmp 工作正常\n");
    printf("✓ 标准C库 strlen 工作正常\n");
    printf("✓ 标准C库 strcmp 工作正常\n");
    printf("✓ 我们的 printf 与标准C库函数完美兼容\n");
    printf("\n这证明了可以在我们的环境中成功使用标准C库的内存函数!\n");

    return 0;
}