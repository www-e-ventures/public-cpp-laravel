# Eloquent in C++ — design notes

> Why the ORM is built the way it is. The recommended path here — a typed struct +
> `Repository<T>` + a hand-written mapper, in-memory backend, repository API — is what's
> implemented (`include/repository.hpp`, `examples/blog/app/models/article.hpp`,
> `examples/blog/tests/test_orm.cpp`); see the Eloquent section in the README. Eloquent is the
> hardest part of Laravel to port because it leans almost entirely on runtime metaprogramming C++
> doesn't have, so this document works through the options and the trade-offs.

## What Eloquent actually is

A bundle of features layered on PHP's runtime magic:

- **ActiveRecord** — a model instance *is* a row; `$user->save()`, `$user->delete()`, static
  finders `User::find(1)`.
- **Magic attributes** — `$user->name` reads/writes an internal `$attributes` array via `__get` /
  `__set`; plus casts, accessors/mutators, and dirty tracking (knowing which columns changed).
- **Query builder** — fluent `User::where('active', 1)->orderBy('name')->get()` returning
  collections of hydrated models.
- **Relationships** — `hasMany` / `belongsTo` / etc. exposed as dynamic properties (`$user->posts`),
  with lazy and eager (`with()`) loading.
- **Late static binding** — `User::find()` returns a `User`, resolved through `static::`.
- **Conventions** — mass assignment (`fillable`/`guarded`), timestamps, soft deletes, model events
  /observers, and migrations/schema builder.

## The core friction (same root cause as the DI finding)

Two C++ facts break the magic, and they are the *same two* that shaped the container:
**no runtime reflection** and **no magic methods**.

1. **Dynamic attribute access.** `$user->name` and `$user->arbitrary_column` need `__get`/`__set`.
   C++ member access is resolved at compile time; there is no hook for "any property name".
2. **Runtime column/type discovery.** Hydration, dirty tracking, and mass assignment all need to
   enumerate a model's columns and their types at runtime. C++ can't introspect a struct's fields.

Everything hard about Eloquent reduces to these two. Everything else ports fine.

## Maps cleanly (the ~70%)

- **Fluent query builder** — just method chaining that returns `*this` and accumulates SQL +
  bound params.
- **Connection abstraction** — a `DB` service/facade over a driver; ties directly to the
  service-provider and facade layers.
- **Explicit CRUD / repository methods** — `find`, `all`, `insert`, `update`, `delete`.
- **Collections** — `std::vector<Model>` + `std::ranges` covers map/filter/pluck ergonomics.

## Needs redesign (the ~30%)

- Magic attribute access (`$model->col`).
- Runtime column/type discovery (for hydrate / dirty / mass-assign).
- Relationships as dynamic properties with lazy loading.

## Design options

### Option A — Typed model struct + repository (abandons ActiveRecord)

```cpp
struct User { int id{}; std::string name; bool active{}; };

// One hand-written mapper per model: the explicit stand-in for reflection.
struct UserMapper {
    static constexpr auto table = "users";
    static User hydrate(const Row&);          // row  -> struct
    static Row  to_row(const User&);          // struct -> row
    // column descriptors (name, member ptr) drive where()/update/dirty
};

Repository<User> users{db};
auto u = users.find(1);
u->name = "Ada";
users.save(*u);
```

- **Pros:** pure std C++, fully typed, compile-checked, trivially testable, zero magic. No hidden
  state (unlike facades). Dirty tracking is explicit/diff-based.
- **Cons:** a mapper per model is mechanical boilerplate; loses the `$user->save()` ActiveRecord
  feel; the column list is restated by hand (echoes the autowiring 'deps still listed by hand').

### Option B — Schema / model codegen

Declare the schema once (a small DSL, or an annotated struct), and generate the model struct +
mapper: column list, types, hydration, dirty tracking, and typed column helpers
(`User::col::name`). This reuses the codegen approach from `tools/autowire/`.

- **Pros:** kills the per-model mapper boilerplate — which is *large and mechanical*, exactly where
  codegen earns its keep. Keeps full typing; can emit typed `where` helpers.
- **Cons:** the build-tooling tax (libclang or a DSL parser, generated artifacts, a schema source
  of truth). **Note the contrast with DI autowiring:** there, codegen wasn't worth it because it
  only saved typing a short dep list. Here the boilerplate it removes is much larger, so the
  cost/benefit may flip — but that's a hypothesis to test, not a foregone conclusion.

### Option C — ActiveRecord-lite via attribute map (reflection-lite)

A `Model` base holding `std::unordered_map<std::string, Value>` plus a declared field schema
(X-macro or a static field-descriptor list). Access via `user.get<std::string>("name")`.

- **Pros:** closest to ActiveRecord dynamics; dirty tracking and mass assignment fall out of the
  attribute map naturally; no codegen.
- **Cons:** **stringly-typed** access — wrong column/type fails at *runtime*, the exact hidden-
  runtime-failure cost flagged for facades. `Value` becomes a `std::variant`/`std::any`, so
  type erasure (and its downsides) returns at the heart of every model. **Not recommended as the
  core.**

## Query builder sketch (shared by all options)

```cpp
auto q = db.table("users")
            .where("active", 1)
            .where("age", ">", 18)
            .order_by("name")
            .limit(10);
// builds: SELECT * FROM users WHERE active = ? AND age > ? ORDER BY name LIMIT 10
// + bound params {1, 18}; execute() returns rows, repo hydrates to User.
```

Fluent chaining is the clean part. The open question is **typed vs type-erased rows**: a typed
result needs the mapper (Option A/B); a generic `Row` (map of column→value) is easier but pushes
type checks to runtime.

## Relationships

Avoid dynamic `$user->posts`. Make relationships explicit methods that return a scoped query:

```cpp
auto posts = users.has_many<Post>(user.id);   // or user.posts(db)
auto authors = posts.belongs_to<User>(...);
```

- **Lazy loading** in ActiveRecord needs the model to hold a back-reference to the connection —
  that couples the model to infrastructure (the classic ActiveRecord smell). The **repository**
  keeps models as plain data and the connection out of them.
- **Eager loading** becomes an explicit `load_with` (batch the FK query) — no `with()` magic, but
  the same N+1 avoidance.

## Recommendation

1. **Start with Option A** for the first vertical slice: typed structs + a hand-written mapper +
   `Repository<T>`. It's pure std C++, fully typed, and honest about the trade-off.
2. **Design the mapper as a codegen target** — make the column-descriptor list a well-defined,
   regular shape so Option B can later generate exactly it without redesigning the API.
3. **Measure, then decide on codegen.** Hand-write one model, count the boilerplate, *then* judge
   Option B with real numbers. Do **not** adopt Option C's stringly-typed core.

## A good first step

- One model + a `Connection` abstraction.
- **Backend: in-memory table first** (zero-dependency); defer real SQL/SQLite until the seam is
  proven. This keeps the first step driver-free.
- `Repository<T>`: `find(id)`, `all()`, `where(col, val)`, `insert`, `update`.
- A hand-written mapper (columns, hydrate, extract).
- Tests: insert→find round-trip, `where` filter, update.
- **Out of scope at first:** relationships, migrations, soft deletes, events.

## Decisions to make first (before writing code)

1. **Backend** — in-memory (zero-dep, the recommended starting point) vs SQLite (real SQL, adds a
   dependency). Affects how much of the query builder is real vs simulated.
2. **API shape** — repository (recommended; clean, testable) vs ActiveRecord feel
   (`model.save()`, couples model to connection). This decision colours the entire public API.
3. **Codegen now or later** — hand-write the first mapper (Option A) and measure, vs commit to
   Option B up front. Recommendation: later, with numbers.

## Headline trade-off

Eloquent's magic is the hardest 30% to port. The C++-honest version trades `$user->name` magic for
typed structs + explicit mappers. The *only* real question is whether codegen regenerates those
mappers — and that's answerable only after one model is built by hand and the boilerplate is
measured. Until there is a reason to start, this is left for later.
