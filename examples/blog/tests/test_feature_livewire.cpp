// test_feature_livewire.cpp — the cpp-livewire endpoints, end-to-end through the kernel.
#include "test_harness.hpp"

#include "bootstrap.hpp"
#include "testing.hpp"

// The /counter page mounts the component (state + rendered view) and loads the client.
TEST(livewire_counter_page_mounts) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.get("/counter")
        .assertOk()
        .assertSee("wire:id=\"counter\"")
        .assertSee("Count: 0")
        .assertSee("wire:click=\"increment\"")
        .assertSee("/livewire.js");
}

// The /livewire endpoint hydrates from posted state, runs the action, re-renders.
TEST(livewire_endpoint_updates_component) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.post("/livewire",
              R"({"name":"counter","action":"increment","arg":"","state":{"count":"4"}})")
        .assertOk()
        .assertSee("\"count\":\"5\"")
        .assertSee("Count: 5");
}

// The JS client is served as a static asset (morphing + wire:model + wire:loading).
TEST(livewire_client_script_served) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.get("/livewire.js")
        .assertOk()
        .assertSee("fetch('/livewire'")
        .assertSee("function morph")
        .assertSee("setLoading");
}

// The counter page carries loading indicators, including an action-targeted one.
TEST(livewire_counter_page_has_loading_indicator) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.get("/counter")
        .assertOk()
        .assertSee("wire:loading")
        .assertSee("wire:target=\"increment\""); // targeted loading element
}

// wire:model: the client folds the input value into state and posts a $refresh; the
// server re-renders with the hydrated state (the unknown action is a harmless no-op).
TEST(livewire_wire_model_state_roundtrips) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.post("/livewire",
              R"({"name":"counter","action":"$refresh","arg":"","state":{"count":"0","name":"Ada"}})")
        .assertOk()
        .assertSee("Hello Ada")          // re-rendered with the bound value
        .assertSee("\"name\":\"Ada\"");  // state echoed back
}

// Keyed list: adding an item renders a wire:key'd <li> (the morph reconciles by key).
TEST(livewire_todos_keyed_list) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.get("/todos").assertOk().assertSee("wire:id=\"todos\"");
    http.post("/livewire",
              R"({"name":"todos","action":"add","arg":"","state":{"items":"","draft":"Milk"}})")
        .assertOk()
        .assertSee("wire:key=\\\"Milk\\\"") // keyed <li> (quotes are JSON-escaped in html)
        .assertSee(">Milk</li>")
        .assertSee("\"items\":\"Milk\"")    // appended to state
        .assertSee("\"draft\":\"\"");       // draft cleared
}

// Nested components: /dashboard mounts two independent counters.
TEST(livewire_dashboard_nests_two_counters) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.get("/dashboard")
        .assertOk()
        .assertSee("wire:id=\"dashboard\"")
        .assertSee("wire:id=\"counter-a\"")
        .assertSee("wire:id=\"counter-b\"")
        .assertSee("wire:component=\"counter\"");
}

// The /counter page exposes the wire:model input.
TEST(livewire_counter_page_has_model_input) {
    auto app = bootstrap();
    HttpClient http(*app.kernel, __fails);

    http.get("/counter").assertOk().assertSee("wire:model=\"name\"").assertSee("Hello stranger");
}
