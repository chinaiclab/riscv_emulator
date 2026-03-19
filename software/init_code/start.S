.section .core0_boot
.global _start
_start:
    # Core 0 startup code - executes from 0x00000000
    # Get core ID using mhartid CSR
    csrr t0, 0xF14    # mhartid CSR
    li t1, 0           # Compare with core 0
    beq t0, t1, core0_entry
    j wait_for_release

core0_entry:
    # Initialize core 0 stack pointer
    la sp, __stack_top_core0
    # Core 0 initialization sequence
    # Old core0_init_phase mechanism removed - MultiCoreMonitor handles coordination

    # Call main function first time (initialization phase)
    call main

    # Re-call main function for parallel execution phase
    # main.c uses initialization_phase flag to distinguish phases
    call main

    # If we reach here, both phases are complete - halt
    j .

wait_for_release:
    # Non-zero cores wait here for release signal
    # Spin on release flag at memory location 0x98000
    li t0, 0x98000
wait_loop:
    lw t1, 0(t0)
    li t2, 0xDEADBEEF
    bne t1, t2, wait_loop

    # Once released, set up core-specific stack pointer
    csrr t0, 0xF14    # Get core ID again
    li t1, 1
    beq t0, t1, setup_core1_sp
    li t1, 2
    beq t0, t1, setup_core2_sp
    li t1, 3
    beq t0, t1, setup_core3_sp
    j default_sp  # Should not happen, but handle gracefully

setup_core1_sp:
    la sp, __stack_top_core1
    j jump_to_main

setup_core2_sp:
    la sp, __stack_top_core2
    j jump_to_main

setup_core3_sp:
    la sp, __stack_top_core3
    j jump_to_main

default_sp:
    # Fallback: use core 0 stack (not ideal but better than crash)
    la sp, __stack_top_core0

jump_to_main:
    # Call main function directly
    # Core#1 will execute main function which handles initialization_phase logic
    call main

    # If we reach here, main has returned - enter infinite loop
    j .

# Note: main function is defined in C code
# No need to define it here in assembly

# External functions
.global release_all_cores
.global setup_other_cores
# core0_init_phase removed - old 0x90004 mechanism
