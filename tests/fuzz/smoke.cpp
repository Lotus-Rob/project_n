// shortcut: wiring check only (nothing parses input yet); replace it with the first real target,
// e.g. the save-file or asset loader, and delete it then.
#include "core/cxx23.hpp" // IWYU pragma: keep

// Declared first: libFuzzer ships no header for it, and -Wmissing-prototypes requires one.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* /*data*/, std::size_t /*size*/) {
    return 0;
}
