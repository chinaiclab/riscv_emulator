#include "tvm_runtime.h"
#include <string.h>

// Forward declarations
int RelaxVMAddTensors(TVMArray* a, TVMArray* b, TVMArray** result);
int RelaxVMMultiplyTensors(TVMArray* a, TVMArray* b, TVMArray** result);

// Relax Operations Implementation for RISC-V
// =========================================

// Addition operation (R.add)
int RelaxVMAdd(TVMFFIAny a, TVMFFIAny b, TVMFFIAny* result) {
    if (!result) return TVM_ERROR_ARGUMENT;

    // Handle different types
    if (a.type_index == 2 && b.type_index == 2) {  // Both floats
        result->type_index = 2;  // Float
        result->value.v_float64 = a.value.v_float64 + b.value.v_float64;
    } else if (a.type_index == 0 && b.type_index == 0) {  // Both integers
        result->type_index = 0;  // Integer
        result->value.v_int64 = a.value.v_int64 + b.value.v_int64;
    } else if (a.type_index == 6 && b.type_index == 6) {  // Both tensors
        return RelaxVMAddTensors((TVMArray*)a.value.v_ptr, (TVMArray*)b.value.v_ptr, (TVMArray**)result);
    } else {
        return TVM_ERROR_ARGUMENT;  // Type mismatch
    }

    return TVM_SUCCESS;
}

// Multiplication operation (R.multiply)
int RelaxVMMultiply(TVMFFIAny a, TVMFFIAny b, TVMFFIAny* result) {
    if (!result) return TVM_ERROR_ARGUMENT;

    // Handle different types
    if (a.type_index == 2 && b.type_index == 2) {  // Both floats
        result->type_index = 2;  // Float
        result->value.v_float64 = a.value.v_float64 * b.value.v_float64;
    } else if (a.type_index == 0 && b.type_index == 0) {  // Both integers
        result->type_index = 0;  // Integer
        result->value.v_int64 = a.value.v_int64 * b.value.v_int64;
    } else if (a.type_index == 6 && b.type_index == 6) {  // Both tensors
        return RelaxVMMultiplyTensors((TVMArray*)a.value.v_ptr, (TVMArray*)b.value.v_ptr, (TVMArray**)result);
    } else {
        return TVM_ERROR_ARGUMENT;  // Type mismatch
    }

    return TVM_SUCCESS;
}

// Tensor addition
int RelaxVMAddTensors(TVMArray* a, TVMArray* b, TVMArray** result) {
    if (!a || !b || !result) return TVM_ERROR_ARGUMENT;

    DLTensor* dl_a = a->dl_tensor;
    DLTensor* dl_b = b->dl_tensor;

    // Check dimensions match
    if (dl_a->ndim != dl_b->ndim) return TVM_ERROR_ARGUMENT;

    for (int i = 0; i < dl_a->ndim; i++) {
        if (dl_a->shape[i] != dl_b->shape[i]) {
            return TVM_ERROR_ARGUMENT;
        }
    }

    // Create result tensor
    if (TVMArrayAlloc(dl_a->shape, dl_a->ndim, dl_a->dtype, dl_a->device, *result) != TVM_SUCCESS) {
        return TVM_ERROR_GENERIC;
    }

    // Perform element-wise addition
    float* data_a = (float*)dl_a->data;
    float* data_b = (float*)dl_b->data;
    float* data_c = (float*)((*result)->dl_tensor->data);

    // Calculate total elements
    int64_t total_elements = 1;
    for (int i = 0; i < dl_a->ndim; i++) {
        total_elements *= dl_a->shape[i];
    }

    // Element-wise addition
    for (int64_t i = 0; i < total_elements; i++) {
        data_c[i] = data_a[i] + data_b[i];
    }

    return TVM_SUCCESS;
}

// Tensor multiplication (element-wise)
int RelaxVMMultiplyTensors(TVMArray* a, TVMArray* b, TVMArray** result) {
    if (!a || !b || !result) return TVM_ERROR_ARGUMENT;

    DLTensor* dl_a = a->dl_tensor;
    DLTensor* dl_b = b->dl_tensor;

    // Check dimensions match
    if (dl_a->ndim != dl_b->ndim) return TVM_ERROR_ARGUMENT;

    for (int i = 0; i < dl_a->ndim; i++) {
        if (dl_a->shape[i] != dl_b->shape[i]) {
            return TVM_ERROR_ARGUMENT;
        }
    }

    // Create result tensor
    if (TVMArrayAlloc(dl_a->shape, dl_a->ndim, dl_a->dtype, dl_a->device, *result) != TVM_SUCCESS) {
        return TVM_ERROR_GENERIC;
    }

    // Perform element-wise multiplication
    float* data_a = (float*)dl_a->data;
    float* data_b = (float*)dl_b->data;
    float* data_c = (float*)((*result)->dl_tensor->data);

    // Calculate total elements
    int64_t total_elements = 1;
    for (int i = 0; i < dl_a->ndim; i++) {
        total_elements *= dl_a->shape[i];
    }

    // Element-wise multiplication
    for (int64_t i = 0; i < total_elements; i++) {
        data_c[i] = data_a[i] * data_b[i];
    }

    return TVM_SUCCESS;
}

// Shape inference helpers
int RelaxVMInferShape(TVMArray* a, TVMArray* b, int64_t** result_shape, int* result_ndim) {
    if (!a || !b || !result_shape || !result_ndim) return TVM_ERROR_ARGUMENT;

    DLTensor* dl_a = a->dl_tensor;
    DLTensor* dl_b = b->dl_tensor;

    // For element-wise operations, shapes must match
    if (dl_a->ndim == dl_b->ndim) {
        // Check if shapes are identical
        int shapes_match = 1;
        for (int i = 0; i < dl_a->ndim; i++) {
            if (dl_a->shape[i] != dl_b->shape[i]) {
                shapes_match = 0;
                break;
            }
        }

        if (shapes_match) {
            *result_ndim = dl_a->ndim;
            *result_shape = (int64_t*)malloc(*result_ndim * sizeof(int64_t));
            if (!*result_shape) return TVM_ERROR_GENERIC;

            for (int i = 0; i < *result_ndim; i++) {
                (*result_shape)[i] = dl_a->shape[i];
            }
            return TVM_SUCCESS;
        }
    }

    // Broadcasting logic would go here for more complex cases
    return TVM_ERROR_ARGUMENT;
}

// Type conversion helpers
int RelaxVMConvertToInt(TVMFFIAny value, int64_t* result) {
    if (!result) return TVM_ERROR_ARGUMENT;

    switch (value.type_index) {
        case 0:  // Integer
            *result = value.value.v_int64;
            break;
        case 2:  // Float
            *result = (int64_t)value.value.v_float64;
            break;
        default:
            return TVM_ERROR_ARGUMENT;
    }

    return TVM_SUCCESS;
}

int RelaxVMConvertToFloat(TVMFFIAny value, double* result) {
    if (!result) return TVM_ERROR_ARGUMENT;

    switch (value.type_index) {
        case 0:  // Integer
            *result = (double)value.value.v_int64;
            break;
        case 2:  // Float
            *result = value.value.v_float64;
            break;
        default:
            return TVM_ERROR_ARGUMENT;
    }

    return TVM_SUCCESS;
}