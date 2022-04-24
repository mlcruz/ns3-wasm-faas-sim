#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <new>

extern "C" {

uint64_t initialize_runtime ();

void register_module (uint64_t runtime_id);

} // extern "C"
