#include "tvm_runtime.h"
#include <stdlib.h>
#include <string.h>

// Simplified Relax VM Interpreter for RISC-V
// =========================================

// VM State
typedef struct {
    VMInstruction* instructions;
    int num_instructions;
    TVMFFIAny* registers;
    int num_registers;
    int pc;  // Program counter
    int running;
} VMState;

// Global VM instance
static VMState vm_state;
static int vm_initialized = 0;

// VM Core Functions
int TVMVMInit(int num_registers) {
    if (vm_initialized) return TVM_SUCCESS;

    vm_state.num_instructions = 0;
    vm_state.instructions = NULL;
    vm_state.num_registers = num_registers;
    vm_state.registers = (TVMFFIAny*)malloc(num_registers * sizeof(TVMFFIAny));
    vm_state.pc = 0;
    vm_state.running = 0;

    if (!vm_state.registers) {
        return TVM_ERROR_GENERIC;
    }

    // Initialize registers
    memset(vm_state.registers, 0, num_registers * sizeof(TVMFFIAny));

    vm_initialized = 1;
    return TVM_SUCCESS;
}

void TVMVMCleanup(void) {
    if (!vm_initialized) return;

    if (vm_state.instructions) {
        free(vm_state.instructions);
    }
    if (vm_state.registers) {
        free(vm_state.registers);
    }

    memset(&vm_state, 0, sizeof(VMState));
    vm_initialized = 0;
}

// Add instruction to VM
int TVMVMAddInstruction(VMOpcode opcode, uint32_t dst, uint32_t src1, uint32_t src2, void* data) {
    if (!vm_initialized) {
        if (TVMVMInit(16) != TVM_SUCCESS) return TVM_ERROR_GENERIC;
    }

    // Reallocate instruction array
    vm_state.instructions = (VMInstruction*)realloc(
        vm_state.instructions,
        (vm_state.num_instructions + 1) * sizeof(VMInstruction)
    );

    if (!vm_state.instructions) return TVM_ERROR_GENERIC;

    // Add new instruction
    VMInstruction* instr = &vm_state.instructions[vm_state.num_instructions];
    instr->opcode = opcode;
    instr->dst = dst;
    instr->src1 = src1;
    instr->src2 = src2;
    instr->data = data;

    vm_state.num_instructions++;
    return TVM_SUCCESS;
}

// Execute single instruction
int TVMVMExecuteInstruction(VMInstruction* instr) {
    switch (instr->opcode) {
        case VM_OP_MOVE:
            vm_state.registers[instr->dst] = vm_state.registers[instr->src1];
            break;

        case VM_OP_LOAD_CONST: {
            TVMFFIAny* const_val = (TVMFFIAny*)instr->data;
            vm_state.registers[instr->dst] = *const_val;
            break;
        }

        case VM_OP_ADD:
            return RelaxVMAdd(vm_state.registers[instr->src1],
                            vm_state.registers[instr->src2],
                            &vm_state.registers[instr->dst]);

        case VM_OP_MULTIPLY:
            return RelaxVMMultiply(vm_state.registers[instr->src1],
                                 vm_state.registers[instr->src2],
                                 &vm_state.registers[instr->dst]);

        case VM_OP_RETURN:
            vm_state.running = 0;
            break;

        case VM_OP_ALLOC: {
            int64_t* shape = (int64_t*)instr->data;
            TVMArray* tensor = TVMCreateFloatTensor(shape, 2, NULL);  // Assume 2D
            vm_state.registers[instr->dst].type_index = 6;  // kTVMNDArrayContainer
            vm_state.registers[instr->dst].value.v_ptr = tensor;
            break;
        }

        case VM_OP_FREE:
            if (vm_state.registers[instr->src1].type_index == 6) {
                TVMFreeTensor((TVMArray*)vm_state.registers[instr->src1].value.v_ptr);
            }
            break;

        default:
            return TVM_ERROR_GENERIC;
    }
    return TVM_SUCCESS;
}

// Execute VM until completion
int TVMVMRun(void) {
    if (!vm_initialized) return TVM_ERROR_GENERIC;

    vm_state.running = 1;
    vm_state.pc = 0;

    while (vm_state.running && vm_state.pc < vm_state.num_instructions) {
        VMInstruction* instr = &vm_state.instructions[vm_state.pc];
        int result = TVMVMExecuteInstruction(instr);

        if (result != TVM_SUCCESS) {
            vm_state.running = 0;
            return result;
        }

        vm_state.pc++;
    }

    return TVM_SUCCESS;
}

// Create a simple vector addition program
int TVMVMCreateVectorAddProgram(void) {
    // Clear previous program
    if (vm_state.instructions) {
        free(vm_state.instructions);
        vm_state.instructions = NULL;
        vm_state.num_instructions = 0;
    }

    // Program for: C[i] = A[i] + B[i] where i = 0..127

    // Instruction 0: Allocate tensor A (128 elements)
    int64_t shape_a[] = {128};
    TVMVMAddInstruction(VM_OP_ALLOC, 0, 0, 0, shape_a);

    // Instruction 1: Allocate tensor B (128 elements)
    int64_t shape_b[] = {128};
    TVMVMAddInstruction(VM_OP_ALLOC, 1, 0, 0, shape_b);

    // Instruction 2: Allocate tensor C (128 elements)
    int64_t shape_c[] = {128};
    TVMVMAddInstruction(VM_OP_ALLOC, 2, 0, 0, shape_c);

    // Instructions 3-130: Vector addition loop
    for (int i = 0; i < 128; i++) {
        // Load A[i] to register 3
        TVMFFIAny const_i = {0, .value.v_int64 = i};
        TVMVMAddInstruction(VM_OP_LOAD_CONST, 3, 0, 0, &const_i);

        // Load B[i] to register 4 (always 1.0)
        TVMFFIAny const_one = {2, .value.v_float64 = 1.0};
        TVMVMAddInstruction(VM_OP_LOAD_CONST, 4, 0, 0, &const_one);

        // Add A[i] + B[i] -> register 5
        TVMVMAddInstruction(VM_OP_ADD, 5, 0, 1, NULL);

        // Store result in C[i]
        TVMVMAddInstruction(VM_OP_MOVE, 2, 5, 0, NULL);
    }

    // Final instruction: Return
    TVMVMAddInstruction(VM_OP_RETURN, 0, 0, 0, NULL);

    return TVM_SUCCESS;
}

// Helper function to create VM for vector addition
TVMVirtualMachine* TVMVMCreate(void* code, void* lib) {
    TVMVMInit(16);  // 16 registers should be enough
    TVMVMCreateVectorAddProgram();

    TVMVirtualMachine* vm = (TVMVirtualMachine*)malloc(sizeof(TVMVirtualMachine));
    vm->code = code;
    vm->global_pool = &vm_state;
    vm->lib = lib;
    return vm;
}

// Execute function on VM
int TVMVMInvoke(TVMVirtualMachine* vm, const char* func_name, TVMFFIAny* args, int num_args, TVMFFIAny* result) {
    // For our simplified VM, we just run the pre-programmed vector addition
    if (strcmp(func_name, "main") == 0) {
        // Copy input arguments to VM registers
        if (num_args >= 2) {
            vm_state.registers[0] = args[0];  // Input A
            vm_state.registers[1] = args[1];  // Input B
        }

        // Execute the program
        int run_result = TVMVMRun();

        // Return result
        if (result && run_result == TVM_SUCCESS) {
            *result = vm_state.registers[2];  // Output C
        }

        return run_result;
    }

    return TVM_ERROR_ARGUMENT;
}