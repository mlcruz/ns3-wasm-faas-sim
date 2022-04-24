#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <new>

enum class ArgType {
  /// Signed 32 bit integer.
  I32,
  /// Signed 64 bit integer.
  I64,
  /// Floating point 32 bit integer.
  F32,
  /// Floating point 64 bit integer.
  F64,
  /// A 128 bit number.
  V128,
  /// A reference to opaque data in the Wasm instance.
  ExternRef,
  /// A reference to a Wasm function.
  FuncRef,
};

enum class StaticModuleList {
  WasmDiv,
  WasmSum,
};

struct WasmArg {
  const char *value;
  ArgType arg_type;
};

struct WasmFunction {
  const char *name;
  WasmArg args[2];
};

extern "C" {

uint64_t initialize_runtime();

char *get_static_module_data(StaticModuleList module);

const char *get_runtime_module_base64_data(uint64_t runtime_id, StaticModuleList module);

const char *register_module(uint64_t runtime_id,
                            const char *module_name,
                            const char *module_data_base_64);

void free_ffi_string(char *data);

bool is_module_registered(uint64_t runtime_id, StaticModuleList module);

int32_t execute_module(uint64_t runtime_id, const char *module_name, WasmFunction function);

} // extern "C"
