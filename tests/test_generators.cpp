// test_generators.cpp — cpp-artisan make:* template rendering (pure functions).
#include "test_harness.hpp"

#include <string>

#include "generators.hpp"

namespace {
bool has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }
} // namespace

TEST(generator_snake_and_table_names) {
    CHECK_EQ(to_snake("Product"), std::string("product"));
    CHECK_EQ(to_snake("OrderItem"), std::string("order_item"));
    CHECK_EQ(table_name("OrderItem"), std::string("order_items"));
}

TEST(generator_render_model) {
    std::string out = render_model("Product");
    CHECK(has(out, "struct Product {"));
    CHECK(has(out, "struct ProductMapper {"));
    CHECK(has(out, "return \"products\";"));
    CHECK(has(out, "using ProductRepository = Repository<Product, ProductMapper>;"));
}

TEST(generator_render_controller) {
    std::string out = render_controller("Order");
    CHECK(has(out, "class OrderController {"));
    CHECK(has(out, "Response index(Request&)"));
    CHECK(has(out, "Response store(Request& req)"));
}

TEST(generator_render_migration) {
    std::string out = render_migration("CreatePostsTable");
    CHECK(has(out, "struct CreatePostsTable : Migration {"));
    CHECK(has(out, "return \"0001_create_posts_table\";"));
    CHECK(has(out, "Schema::create(c, \"table_name\""));
    CHECK(has(out, "void down(Connection& c)"));
    CHECK(has(out, "Schema::drop_if_exists"));
}
