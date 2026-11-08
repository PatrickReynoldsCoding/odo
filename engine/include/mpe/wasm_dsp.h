// Instruments as WebAssembly, run by wasm3: the interpreter way. A Faust instrument compiled with
// `faust -lang wasm` (engine/tools/wasm_instruments.py) is portable code a store allows an app to
// download, unlike native code. Each voice is its own wasm3 instance; the engine sees an ordinary
// Faust dsp. For the interpreter experiment (docs/vision/VISION_PLAN.md decision 3).
#pragma once
#include <string>

namespace mpe {

// Add an instrument from its WebAssembly and Faust's JSON description of it (and of its `post`,
// if it has one: else empty strings). Returns its index in the instrument table, or -1 and *error.
int loadWasmInstrument(const std::string& id, const std::string& wasm, const std::string& json,
                       const std::string& postWasm, const std::string& postJson, std::string* error = nullptr);

}  // namespace mpe
