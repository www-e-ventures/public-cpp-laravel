// host.cpp — a C++ program that embeds the Wasmtime runtime and drives the framework
// compiled to WebAssembly. C++ running C++-compiled-to-WASM, with zero JavaScript.
//
// Loads the standalone WASI reactor (wasm/dist/lipp_reactor.wasm), then for each request
// writes the method/path/body strings into the module's linear memory (via its exported
// malloc), calls lipp_handle, and reads the response string back out.
#include <wasmtime.hh>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace wasmtime;

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int main() {
    Config config;
    config.wasm_exceptions(true); // the reactor is built with wasm exception handling
    Engine engine(std::move(config));
    Store store(engine);

    WasiConfig wasi;
    wasi.inherit_stdout();
    wasi.inherit_stderr();
    store.context().set_wasi(std::move(wasi)).unwrap();

    Linker linker(engine);
    linker.define_wasi().unwrap();

    auto bytes = read_file("wasm/dist/lipp_reactor.wasm");
    Module module = Module::compile(engine, bytes).unwrap();
    Instance instance = linker.instantiate(store, module).unwrap();

    // Reactor modules expose _initialize for setup (run static constructors, etc.).
    if (auto init = instance.get(store, "_initialize"))
        std::get<Func>(*init).call(store, {}).unwrap();

    auto memory = std::get<Memory>(*instance.get(store, "memory"));
    auto malloc_f = std::get<Func>(*instance.get(store, "malloc"));
    auto free_f = std::get<Func>(*instance.get(store, "free"));
    auto handle_f = std::get<Func>(*instance.get(store, "lipp_handle"));
    auto status_f = std::get<Func>(*instance.get(store, "lipp_status"));

    auto write_string = [&](const std::string& s) -> int32_t {
        int32_t ptr = malloc_f.call(store, {Val(int32_t(s.size() + 1))}).unwrap()[0].i32();
        auto mem = memory.data(store);
        std::memcpy(mem.data() + ptr, s.data(), s.size());
        mem[ptr + s.size()] = 0;
        return ptr;
    };
    auto read_string = [&](int32_t ptr) -> std::string {
        auto mem = memory.data(store);
        std::string out;
        for (size_t i = size_t(ptr); i < mem.size() && mem[i] != 0; ++i) out += char(mem[i]);
        return out;
    };

    auto request = [&](const std::string& method, const std::string& path,
                       const std::string& body) {
        int32_t pm = write_string(method), pp = write_string(path), pb = write_string(body);
        int32_t res = handle_f.call(store, {Val(pm), Val(pp), Val(pb)}).unwrap()[0].i32();
        std::string out = read_string(res);
        int status = status_f.call(store, {}).unwrap()[0].i32();
        free_f.call(store, {Val(pm)}).unwrap();
        free_f.call(store, {Val(pp)}).unwrap();
        free_f.call(store, {Val(pb)}).unwrap();
        std::printf("%-5s %-16s -> HTTP %d  %s\n", method.c_str(), path.c_str(), status,
                    out.c_str());
    };

    std::printf("C++ host -> Wasmtime -> the framework compiled to WASM (no JS):\n\n");
    request("GET", "/articles", "");
    request("GET", "/articles/2", "");
    request("POST", "/articles", "title=Created from a C++ host&published=1");
    request("GET", "/articles.html", "");
    request("POST", "/articles", "published=1"); // 422
    request("GET", "/nope", "");                 // 404
    return 0;
}
