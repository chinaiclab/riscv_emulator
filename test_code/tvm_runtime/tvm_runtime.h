#ifndef TVM_RUNTIME_H
#define TVM_RUNTIME_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// TVM Runtime Core Types for RISC-V Emulator
// =========================================

// Device types (simplified)
typedef enum {
    kDLCPU = 1,
    kDLCUDA = 2,
    kDLOpenCL = 4,
    kDLVulkan = 7,
    kDLMetal = 8,
    kDLVPI = 9,
    kDLROCM = 10,
    kDLCUDAManager = 13,
    kDLExtDev = 12,
} DLDeviceType;

// Data type codes
typedef enum {
    kDLInt = 0,
    kDLUInt = 1,
    kDLFloat = 2,
    kDLBfloat = 4,
    kDLComplex = 5,
} DLDataTypeCode;

// Data type structure
typedef struct {
    uint8_t code;
    uint8_t bits;
    uint16_t lanes;
} DLDataType;

// Device structure
typedef struct {
    DLDeviceType device_type;
    int32_t device_id;
} DLDevice;

// Tensor array structure
typedef struct {
    void* data;
    DLDevice device;
    int32_t ndim;
    int64_t* shape;
    int64_t* strides;
    DLDataType dtype;
    int64_t byte_offset;
} DLTensor;

// TVM Array (simplified wrapper around DLTensor)
typedef struct {
    DLTensor* dl_tensor;
} TVMArray;

// TVM Function Handle
typedef void* TVMFunctionHandle;

// TVM Module Handle
typedef void* TVMModuleHandle;

// TVM FFI Any type (for generic values)
typedef struct {
    int type_index;
    union {
        int64_t v_int64;
        double v_float64;
        void* v_ptr;
        void* v_handle;
    } value;
} TVMFFIAny;

// VM Specific Types
typedef struct {
    void* code;
    void* global_pool;
    void* lib;
} TVMVirtualMachine;

// VM Instruction types
typedef enum {
    VM_OP_MOVE = 0,
    VM_OP_LOAD_CONST = 1,
    VM_OP_INVOKE = 2,
    VM_OP_RETURN = 3,
    VM_OP_ADD = 4,
    VM_OP_MULTIPLY = 5,
    VM_OP_ALLOC = 6,
    VM_OP_FREE = 7,
} VMOpcode;

// VM Instruction
typedef struct {
    VMOpcode opcode;
    uint32_t dst;
    uint32_t src1;
    uint32_t src2;
    void* data;
} VMInstruction;

// Memory Management Functions
void* TVMBackendAllocWorkspace(DLDevice device, uint64_t nbytes);
void TVMBackendFreeWorkspace(DLDevice device, void* ptr);

// Basic Operations
int TVMArrayAlloc(const int64_t* shape, int ndim, DLDataType dtype, DLDevice dev, TVMArray* out);
void TVMArrayFree(TVMArray* handle);
void* TVMArrayGetData(TVMArray* handle);

// Function Interface
int TVMFuncCall(TVMFunctionHandle func, TVMFFIAny* args, int* type_codes, int num_args, TVMFFIAny* out, int* out_type_code);

// Module Interface
int TVMModLoadFromFile(const char* filename, const char* format, TVMModuleHandle* out);
int TVMModGetFunction(TVMModuleHandle mod, const char* func_name, int query_imports, TVMFunctionHandle* out);

// VM Interface
TVMVirtualMachine* TVMVMCreate(void* code, void* lib);
int TVMVMInvoke(TVMVirtualMachine* vm, const char* func_name, TVMFFIAny* args, int num_args, TVMFFIAny* result);

// Relax VM Specific Functions
int RelaxVMAdd(TVMFFIAny a, TVMFFIAny b, TVMFFIAny* result);
int RelaxVMMultiply(TVMFFIAny a, TVMFFIAny b, TVMFFIAny* result);
int RelaxVMAddTensors(TVMArray* a, TVMArray* b, TVMArray** result);
int RelaxVMMultiplyTensors(TVMArray* a, TVMArray* b, TVMArray** result);

// VM Management Functions
int TVMVMInit(int num_registers);
void TVMVMCleanup(void);
int TVMVMAddInstruction(VMOpcode opcode, uint32_t dst, uint32_t src1, uint32_t src2, void* data);

// Simplified tensor creation helpers
TVMArray* TVMCreateTensor(int64_t* shape, int ndim, void* data);
TVMArray* TVMCreateFloatTensor(int64_t* shape, int ndim, float* data);
void TVMFreeTensor(TVMArray* tensor);

// Error handling
typedef enum {
    TVM_SUCCESS = 0,
    TVM_ERROR_GENERIC = -1,
    TVM_ERROR_ARGUMENT = -2,
    TVM_ERROR_INDEX_OUT_OF_BOUNDS = -3,
} TVMRetValue;

// RISC-V specific definitions
#define TVM_RISCV_STACK_SIZE 4096
#define TVM_RISCV_HEAP_SIZE 1024*1024  // 1MB
#define TVM_RISCV_MAX_INSTRUCTIONS 1000

#endif // TVM_RUNTIME_H