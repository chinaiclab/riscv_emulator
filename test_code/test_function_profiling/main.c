#include <stdint.h>
#include "../../software/init_code/vprintf.h"

extern uint32_t get_core_id(void);

// Function to print a number in hex format
void print_hex(uint32_t num) {
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[9]; // 8 hex digits + null terminator
    buffer[8] = '\0';

    for (int i = 7; i >= 0; i--) {
        buffer[i] = hex_chars[num & 0xF];
        num >>= 4;
    }

    printf(buffer);
}

// Function to print a string followed by a number in hex
void print_result(const char* label, uint32_t value) {
    printf(label);
    print_hex(value);
    printf("\n");
}

// Function to perform matrix multiplication - memory intensive operation
void matrix_multiply(int size, int* a, int* b, int* result) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            int sum = 0;
            for (int k = 0; k < size; k++) {
                sum += a[i * size + k] * b[k * size + j];
            }
            result[i * size + j] = sum;
        }
    }
}

// Function to perform bubble sort - memory intensive operation
void bubble_sort(int* arr, int size) {
    for (int i = 0; i < size - 1; i++) {
        for (int j = 0; j < size - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                // Swap elements
                int temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

// Function to calculate Fibonacci sequence - memory intensive operation
int fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    int a = 0, b = 1, c;
    for (int i = 2; i <= n; i++) {
        c = a + b;
        a = b;
        b = c;
    }
    return b;
}

// Function to calculate factorial - memory intensive operation
int factorial(int n) {
    if (n <= 1) {
        return 1;
    }
    int result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

// Function to perform binary search - memory intensive operation
int binary_search(int* arr, int size, int target) {
    int left = 0, right = size - 1;
    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (arr[mid] == target) {
            return mid;
        } else if (arr[mid] < target) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return -1; // Not found
}

// Function to perform quick sort (helper function) - memory intensive operation
void quick_sort_helper(int* arr, int low, int high) {
    if (low < high) {
        int pivot = arr[high];
        int i = low - 1;
        
        for (int j = low; j < high; j++) {
            if (arr[j] <= pivot) {
                i++;
                // Swap elements
                int temp = arr[i];
                arr[i] = arr[j];
                arr[j] = temp;
            }
        }
        
        // Swap elements
        int temp = arr[i + 1];
        arr[i + 1] = arr[high];
        arr[high] = temp;
        
        int pi = i + 1;
        
        quick_sort_helper(arr, low, pi - 1);
        quick_sort_helper(arr, pi + 1, high);
    }
}

// Function to perform quick sort - memory intensive operation
void quick_sort(int* arr, int size) {
    quick_sort_helper(arr, 0, size - 1);
}

// Function to calculate prime numbers up to n using Sieve of Eratosthenes - memory intensive operation
void sieve_of_eratosthenes(int n, int* primes) {
    // Use a memory location for temporary storage
    // This will create memory access patterns that affect cache performance
    int* is_prime = (int*)0x200000; // Use a memory location for temporary storage
    for (int i = 0; i <= n; i++) {
        is_prime[i] = 1;
    }
    is_prime[0] = is_prime[1] = 0;
    
    for (int i = 2; i * i <= n; i++) {
        if (is_prime[i]) {
            for (int j = i * i; j <= n; j += i) {
                is_prime[j] = 0;
            }
        }
    }
    
    int count = 0;
    for (int i = 2; i <= n; i++) {
        if (is_prime[i]) {
            primes[count++] = i;
        }
    }
}

// Function to perform memory-intensive operations to stress cache
void memory_stress_test() {
    // Create a large array to stress the cache
    int* large_array = (int*)0x300000; // Use a memory location for large array
    int array_size = 1024; // 4KB array (1024 * 4 bytes)
    
    // Initialize the array
    for (int i = 0; i < array_size; i++) {
        large_array[i] = i;
    }
    
    // Perform operations that will cause cache misses
    for (int i = 0; i < array_size; i += 16) { // Access every 16th element to cause cache misses
        large_array[i] *= 2;
    }
    
    // Perform operations that will cause TLB misses
    for (int i = 0; i < array_size; i += 64) { // Access every 64th element to cause more misses
        large_array[i] += fibonacci(10);
    }
}

void main(void) {
    uint32_t core_id = get_core_id();
    printf("Comprehensive Memory and Cache Profiling Test Started\n");
    printf("Core ID: ");
    print_hex(core_id);
    printf("\n");

    // Initialize test data
    int matrix_a[16];
    matrix_a[0] = 1; matrix_a[1] = 2; matrix_a[2] = 3; matrix_a[3] = 4;
    matrix_a[4] = 5; matrix_a[5] = 6; matrix_a[6] = 7; matrix_a[7] = 8;
    matrix_a[8] = 9; matrix_a[9] = 10; matrix_a[10] = 11; matrix_a[11] = 12;
    matrix_a[12] = 13; matrix_a[13] = 14; matrix_a[14] = 15; matrix_a[15] = 16;

    int matrix_b[16];
    matrix_b[0] = 16; matrix_b[1] = 15; matrix_b[2] = 14; matrix_b[3] = 13;
    matrix_b[4] = 12; matrix_b[5] = 11; matrix_b[6] = 10; matrix_b[7] = 9;
    matrix_b[8] = 8; matrix_b[9] = 7; matrix_b[10] = 6; matrix_b[11] = 5;
    matrix_b[12] = 4; matrix_b[13] = 3; matrix_b[14] = 2; matrix_b[15] = 1;

    int matrix_result[16];

    int sort_array[10];
    sort_array[0] = 64; sort_array[1] = 34; sort_array[2] = 25; sort_array[3] = 12;
    sort_array[4] = 22; sort_array[5] = 11; sort_array[6] = 90; sort_array[7] = 88;
    sort_array[8] = 76; sort_array[9] = 50;

    int search_array[10];
    search_array[0] = 1; search_array[1] = 3; search_array[2] = 5; search_array[3] = 7;
    search_array[4] = 9; search_array[5] = 11; search_array[6] = 13; search_array[7] = 15;
    search_array[8] = 17; search_array[9] = 19;

    int primes[50];

    // Test matrix multiplication
    printf("Testing matrix multiplication...\n");
    matrix_multiply(4, matrix_a, matrix_b, matrix_result);
    print_result("Matrix multiplication result[0]: ", matrix_result[0]);
    print_result("Matrix multiplication result[15]: ", matrix_result[15]);

    // Test bubble sort
    printf("Testing bubble sort...\n");
    bubble_sort(sort_array, 10);
    print_result("Bubble sort result[0]: ", sort_array[0]);
    print_result("Bubble sort result[9]: ", sort_array[9]);

    // Test Fibonacci
    printf("Testing Fibonacci...\n");
    int fib_result = fibonacci(10);
    print_result("Fibonacci(10): ", fib_result);

    // Test factorial
    printf("Testing factorial...\n");
    int fact_result = factorial(7);
    print_result("Factorial(7): ", fact_result);

    // Test binary search
    printf("Testing binary search...\n");
    int search_result = binary_search(search_array, 10, 11);
    print_result("Binary search for 11: ", search_result);

    // Test quick sort
    printf("Testing quick sort...\n");
    quick_sort(sort_array, 10);
    print_result("Quick sort result[0]: ", sort_array[0]);
    print_result("Quick sort result[9]: ", sort_array[9]);

    // Test sieve of Eratosthenes
    printf("Testing sieve of Eratosthenes...\n");
    sieve_of_eratosthenes(30, primes);
    print_result("First prime: ", primes[0]);
    print_result("Last prime <= 30: ", primes[9]); // There are 10 primes <= 30

    // Perform memory stress test to demonstrate cache and memory performance
    printf("Performing memory stress test...\n");
    memory_stress_test();
    printf("Memory stress test completed\n");

    printf("Comprehensive Memory and Cache Profiling Test Completed\n");
    
    // All cores enter infinite loop at the end
    while (1) {
        // Core-specific work can continue here
    }
}