// tvm target: c -keys=cpu 
#define TVM_EXPORTS
#include "tvm/runtime/base.h"
#include "tvm/runtime/c_backend_api.h"
#include "tvm/ffi/c_api.h"
#include <math.h>
#include <stdbool.h>
void* __tvm_ffi__library_ctx = NULL;
#ifdef __cplusplus
extern "C"
#endif
TVM_DLL int32_t __tvm_ffi_main(void* self_handle, void* args, int32_t num_args, void* result);
#ifdef __cplusplus
extern "C"
#endif
TVM_DLL int32_t __tvm_ffi_main(void* self_handle, void* args, int32_t num_args, void* result) {
  int32_t var_A_type_index = (((TVMFFIAny*)args)[0].type_index);
  int32_t var_B_type_index = (((TVMFFIAny*)args)[1].type_index);
  int32_t var_C_type_index = (((TVMFFIAny*)args)[2].type_index);
  void* var_A = ((var_A_type_index == 70) ? ((void*)((char*)(((TVMFFIAny*)args)[0].v_ptr) + 24)) : (((TVMFFIAny*)args)[0].v_ptr));
  void* var_B = ((var_B_type_index == 70) ? ((void*)((char*)(((TVMFFIAny*)args)[1].v_ptr) + 24)) : (((TVMFFIAny*)args)[1].v_ptr));
  void* var_C = ((var_C_type_index == 70) ? ((void*)((char*)(((TVMFFIAny*)args)[2].v_ptr) + 24)) : (((TVMFFIAny*)args)[2].v_ptr));
  void* main_var_A_shape = (((DLTensor*)var_A)[0].shape);
  void* main_var_A_strides = (((DLTensor*)var_A)[0].strides);
  int32_t dev_id = (((DLTensor*)var_A)[0].device.device_id);
  void* A = (((DLTensor*)var_A)[0].data);
  void* main_var_B_shape = (((DLTensor*)var_B)[0].shape);
  void* main_var_B_strides = (((DLTensor*)var_B)[0].strides);
  void* B = (((DLTensor*)var_B)[0].data);
  void* main_var_C_shape = (((DLTensor*)var_C)[0].shape);
  void* main_var_C_strides = (((DLTensor*)var_C)[0].strides);
  void* C = (((DLTensor*)var_C)[0].data);
  if (!(main_var_A_strides == NULL)) {
  }
  if (!(main_var_B_strides == NULL)) {
  }
  if (!(main_var_C_strides == NULL)) {
  }
  for (int32_t i = 0; i < 128; ++i) {
    ((float*)C)[i] = (((float*)A)[i] + ((float*)B)[i]);
  }
  return 0;
}


