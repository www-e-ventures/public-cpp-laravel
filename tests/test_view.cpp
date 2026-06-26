// test_view.cpp — the Blade-lite template engine.
#include "test_harness.hpp"

#include <string>

#include "view.hpp"

TEST(view_escaped_interpolation) {
    View v;
    v.set("name", "<b>Ada</b>");
    CHECK_EQ(render("Hi {{ name }}!", v), std::string("Hi &lt;b&gt;Ada&lt;/b&gt;!"));
}

TEST(view_raw_interpolation) {
    View v;
    v.set("html", "<i>x</i>");
    CHECK_EQ(render("[{!! html !!}]", v), std::string("[<i>x</i>]"));
}

TEST(view_if_directive) {
    View on;
    on.set("admin", "1");
    CHECK_EQ(render("@if(admin)Hello admin@endif", on), std::string("Hello admin"));

    View off;
    off.set("admin", "0");
    CHECK_EQ(render("@if(admin)Hello admin@endif", off), std::string(""));
}

TEST(view_foreach_directive) {
    View ctx;
    View a;
    a.set("title", "Alpha");
    View b;
    b.set("title", "Bravo");
    ctx.lists["posts"] = {a, b};

    CHECK_EQ(render("@foreach(posts as p)<li>{{ title }}</li>@endforeach", ctx),
             std::string("<li>Alpha</li><li>Bravo</li>"));
}

TEST(view_if_else) {
    View on;
    on.set("admin", "1");
    CHECK_EQ(render("@if(admin)yes@elseno@endif", on), std::string("yes"));
    View off;
    off.set("admin", "0");
    CHECK_EQ(render("@if(admin)yes@elseno@endif", off), std::string("no"));
}

TEST(view_include_partial) {
    Views v;
    v.define("greeting", "Hi {{ name }}");
    v.define("page", "[@include('greeting')]");
    View ctx;
    ctx.set("name", "Ada");
    CHECK_EQ(v.render("page", ctx), std::string("[Hi Ada]"));
}

TEST(view_layout_extends_section_yield) {
    Views v;
    v.define("layout", "<html><body>@yield('content')</body></html>");
    v.define("home", "@extends('layout')@section('content')Hello {{ name }}@endsection");
    View ctx;
    ctx.set("name", "Ada");
    CHECK_EQ(v.render("home", ctx), std::string("<html><body>Hello Ada</body></html>"));
}

TEST(view_nested_foreach_and_if) {
    View ctx;
    View published;
    published.set("title", "Shown");
    published.set("live", "1");
    View draft;
    draft.set("title", "Hidden");
    draft.set("live", "0");
    ctx.lists["posts"] = {published, draft};

    std::string out =
        render("@foreach(posts as p)@if(live){{ title }};@endif@endforeach", ctx);
    CHECK_EQ(out, std::string("Shown;")); // only the live one renders
}
