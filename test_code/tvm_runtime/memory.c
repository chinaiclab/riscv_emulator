#include "tvm_runtime.h"
#include <stdlib.h>
#include <string.h>

// Simple memory allocator for RISC-V TVM Runtime
// ===========================================

// Global heap for TVM runtime
static char tvm_heap[TVM_RISCV_HEAP_SIZE];
static size_t heap_offset = 0;
static int memory_initialized = 0;

// Memory management functions
void TVMInitMemory(void) {
    if (!memory_initialized) {
        memset(tvm_heap, 0, TVM_RISCV_HEAP_SIZE);
        heap_offset = 0;
        memory_initialized = 1;
    }
}

void* TVMBackendAllocWorkspace(DLDevice device, uint64_t nbytes) {
    TVMInitMemory();

    // Align to 8-byte boundary
    uint64_t aligned_size = (nbytes + 7) & ~7;

    if (heap_offset + aligned_size > TVM_RISCV_HEAP_SIZE) {
        return NULL;  // Out of memory
    }

    void* ptr = &tvm_heap[heap_offset];
    heap_offset += aligned_size;
    return ptr;
}

void TVMBackendFreeWorkspace(DLDevice device, void* ptr) {
    // Simple implementation - we don't actually free individual allocations
    // In a more sophisticated implementation, we would maintain a free list
    // For now, this is sufficient for the RISC-V emulator use case
}

// Array management
int TVMArrayAlloc(const int64_t* shape, int ndim, DLDataType dtype, DLDevice dev, TVMArray* out) {
    if (!out || !shape) return TVM_ERROR_ARGUMENT;

    // Calculate total bytes needed
    int64_t total_elements = 1;
    for (int i = 0; i < ndim; i++) {
        total_elements *= shape[i];
    }

    int64_t bytes_per_element = (dtype.bits + 7) / 8;
    int64_t total_bytes = total_elements * bytes_per_element;

    // Allocate DLTensor structure
    DLTensor* dl_tensor = (DLTensor*)malloc(sizeof(DLTensor));
    if (!dl_tensor) return TVM_ERROR_GENERIC;

    // Allocate shape array
    int64_t* shape_array = (int64_t*)malloc(ndim * sizeof(int64_t));
    if (!shape_array) {
        free(dl_tensor);
        return TVM_ERROR_GENERIC;
    }

    // Copy shape
    for (int i = 0; i < ndim; i++) {
        shape_array[i] = shape[i];
    }

    // Allocate data
    void* data = TVMBackendAllocWorkspace(dev, total_bytes);
    if (!data) {
        free(shape_array);
        free(dl_tensor);
        return TVM_ERROR_GENERIC;
    }

    // Initialize DLTensor
    dl_tensor->data = data;
    dl_tensor->device = dev;
    dl_tensor->ndim = ndim;
    dl_tensor->shape = shape_array;
    dl_tensor->strides = NULL;  // Default to dense layout
    dl_tensor->dtype = dtype;
    dl_tensor->byte_offset = 0;

    // Initialize TVMArray
    out->dl_tensor = dl_tensor;

    return TVM_SUCCESS;
}

void TVMArrayFree(TVMArray* handle) {
    if (!handle || !handle->dl_tensor) return;

    DLTensor* dl_tensor = handle->dl_tensor;

    // Free shape array
    if (dl_tensor->shape) {
        free((void*)dl_tensor->shape);
    }

    // Free strides if allocated
    if (dl_tensor->strides) {
        free((void*)dl_tensor->strides);
    }

    // Free data memory (handled by workspace allocator)
    TVMBackendFreeWorkspace(dl_tensor->device, dl_tensor->data);

    // Free DLTensor structure
    free(dl_tensor);

    // Clear handle
    handle->dl_tensor = NULL;
}

void* TVMArrayGetData(TVMArray* handle) {
    if (!handle || !handle->dl_tensor) return NULL;
    return handle->dl_tensor->data;
}

// Helper functions for creating tensors
TVMArray* TVMCreateTensor(int64_t* shape, int ndim, void* data) {
    TVMArray* array = (TVMArray*)malloc(sizeof(TVMArray));
    if (!array) return NULL;

    DLDevice dev = {kDLCPU, 0};
    DLDataType dtype = {kDLFloat, 32, 1};  // Default to float32

    if (TVMArrayAlloc(shape, ndim, dtype, dev, array) != TVM_SUCCESS) {
        free(array);
        return NULL;
    }

    // Copy data if provided
    if (data) {
        void* dst = TVMArrayGetData(array);
        int64_t total_elements = 1;
        for (int i = 0; i < ndim; i++) {
            total_elements *= shape[i];
        }
        memcpy(dst, data, total_elements * sizeof(float));
    }

    return array;
}

TVMArray* TVMCreateFloatTensor(int64_t* shape, int ndim, float* data) {
    return TVMCreateTensor(shape, ndim, (void*)data);
}

void TVMFreeTensor(TVMArray* tensor) {
    if (tensor) {
        TVMArrayFree(tensor);
        free(tensor);
    }
}