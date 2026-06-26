<?php
// HttpTestCase — a tiny Laravel/Illuminate\Testing-flavoured base over plain phpunit
// + curl. The same suite runs against any BASE_URL, so it's backend-agnostic (the
// C++ server or the PHP reference app).

use PHPUnit\Framework\Assert;
use PHPUnit\Framework\TestCase;

final class TestResponse {
    public function __construct(public int $status, public string $body) {}

    public function assertStatus(int $code): self {
        Assert::assertSame($code, $this->status, "unexpected status; body: {$this->body}");
        return $this;
    }
    public function assertOk(): self { return $this->assertStatus(200); }
    public function assertCreated(): self { return $this->assertStatus(201); }
    public function assertUnauthorized(): self { return $this->assertStatus(401); }
    public function assertUnprocessable(): self { return $this->assertStatus(422); }
    public function assertNotFound(): self { return $this->assertStatus(404); }

    public function assertSee(string $text): self {
        Assert::assertStringContainsString($text, $this->body);
        return $this;
    }
    public function json(): array { return json_decode($this->body, true) ?? []; }

    public function assertJsonFragment(array $subset): self {
        $data = $this->json();
        foreach ($subset as $k => $v) {
            Assert::assertArrayHasKey($k, $data);
            Assert::assertEquals($v, $data[$k]);
        }
        return $this;
    }
}

abstract class HttpTestCase extends TestCase {
    protected function baseUrl(): string {
        return rtrim(getenv('BASE_URL') ?: 'http://127.0.0.1:8080', '/');
    }

    protected function request(string $method, string $uri, ?string $body, array $headers): TestResponse {
        $ch = curl_init($this->baseUrl() . $uri);
        $hdr = [];
        foreach ($headers as $k => $v) $hdr[] = "$k: $v";
        curl_setopt_array($ch, [
            CURLOPT_RETURNTRANSFER => true,
            CURLOPT_CUSTOMREQUEST => $method,
            CURLOPT_HTTPHEADER => $hdr,
        ]);
        if ($body !== null) curl_setopt($ch, CURLOPT_POSTFIELDS, $body);
        $resp = curl_exec($ch);
        $code = (int) curl_getinfo($ch, CURLINFO_HTTP_CODE);
        curl_close($ch);
        return new TestResponse($code, $resp === false ? '' : $resp);
    }

    protected function get(string $uri, array $headers = []): TestResponse {
        return $this->request('GET', $uri, null, $headers);
    }
    protected function post(string $uri, ?string $body = null, array $headers = []): TestResponse {
        return $this->request('POST', $uri, $body, $headers);
    }
}
