<?php
// ArticlesParityTest — Laravel-style feature tests run against BASE_URL. The parity
// script runs this same suite against the C++ server and the PHP reference app; both
// must pass identically. Tests are self-contained (capture the created id) so they
// need no DB reset and don't depend on global state.

final class ArticlesParityTest extends HttpTestCase {
    private const AUTH = ['Authorization' => 'secret-token'];

    public function test_health(): void {
        $this->get('/health')->assertOk()->assertSee('ok');
    }

    public function test_create_then_show(): void {
        $created = $this->post('/articles', 'title=Parity&views=4&published=1', self::AUTH)
            ->assertCreated()
            ->assertJsonFragment(['title' => 'Parity', 'views' => 4, 'published' => true]);

        $id = $created->json()['id'];
        $this->get("/articles/{$id}")
            ->assertOk()
            ->assertJsonFragment(['id' => $id, 'title' => 'Parity', 'views' => 4]);
    }

    public function test_create_requires_auth(): void {
        $this->post('/articles', 'title=NoAuth')->assertUnauthorized()->assertSee('unauthorized');
    }

    public function test_validation_rejects_empty_title(): void {
        $this->post('/articles', 'views=9&published=1', self::AUTH)
            ->assertUnprocessable()
            ->assertSee('title is required');
    }

    public function test_missing_returns_404(): void {
        $this->get('/articles/999999')->assertNotFound()->assertSee('not found');
    }
}
