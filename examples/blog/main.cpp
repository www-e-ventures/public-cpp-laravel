// main.cpp — demo entry point. Runs the same five scenarios the original slice
// proved in its main(); these are also asserted in the test suite (tests/).
#include <iostream>
#include <string>

#include "bootstrap.hpp"
#include "facades.hpp"
#include "models/article.hpp"
#include "user_service.hpp"

int main() {
    App app = bootstrap();
    Kernel& kernel = *app.kernel;

    auto dump = [](const std::string& label, const Response& r) {
        std::cout << label << "  status=" << r.status
                  << "  ct=" << r.headers.at("Content-Type")
                  << "  body=" << r.body << "\n";
    };

    // 1. Happy path, authorised.
    dump("auth+found ", kernel.handle({"GET", "/users/42", {}, {{"Authorization", "secret-token"}}, ""}));
    // 2. Authorised but missing record.
    dump("auth+miss  ", kernel.handle({"GET", "/users/7", {}, {{"Authorization", "secret-token"}}, ""}));
    // 3. Missing token → middleware short-circuits before controller.
    dump("no-token   ", kernel.handle({"GET", "/users/42", {}, {}, ""}));
    // 4. Unrouted path.
    dump("404        ", kernel.handle({"GET", "/nope", {}, {}, ""}));
    // 5. Singleton proof: same UserService instance across two resolves.
    std::cout << "singleton  same_instance="
              << (app.container->resolve<UserService>().get() ==
                  app.container->resolve<UserService>().get())
              << "\n";
    // 6. Facade proof: DB::query()-style static proxy resolves via the container.
    std::cout << "facade     Users::find(42)=" << Users::find("42") << "\n";
    // 7. ORM slice: resolve a repository, persist a model, read it back.
    auto articles = app.container->resolve<ArticleRepository>();
    Article post{0, "Hello, persistence", 0, true};
    articles->insert(post);
    auto fetched = articles->find(post.id);
    std::cout << "orm        inserted id=" << post.id
              << " found_title=" << (fetched ? fetched->title : "<none>") << "\n";
    return 0;
}
