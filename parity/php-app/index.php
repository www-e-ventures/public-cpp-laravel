<?php
// Vanilla-PHP reference app mirroring the C++ Articles resource, byte-for-byte on
// responses. Serves as the behavioural oracle: the same phpunit suite runs against
// this and the C++ server and must pass identically.
//   php -S 127.0.0.1:8092 parity/php-app/index.php

$dbfile = getenv('PARITY_DB') ?: (sys_get_temp_dir() . '/parity_articles.sqlite');
$pdo = new PDO('sqlite:' . $dbfile);
$pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$pdo->exec('CREATE TABLE IF NOT EXISTS articles (
    id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT, views INTEGER, published INTEGER)');

$method = $_SERVER['REQUEST_METHOD'];
$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);

function send_json($data, int $code = 200): void {
    http_response_code($code);
    header('Content-Type: application/json');
    echo json_encode($data);
    exit;
}
function article_json(array $row): array {
    return ['id' => (int) $row['id'], 'title' => $row['title'],
            'views' => (int) $row['views'], 'published' => (bool) $row['published']];
}

if ($method === 'GET' && $path === '/health') {
    header('Content-Type: text/plain');
    echo 'ok';
    exit;
}

if ($method === 'GET' && $path === '/articles') {
    $rows = $pdo->query('SELECT * FROM articles')->fetchAll(PDO::FETCH_ASSOC);
    send_json(array_map('article_json', $rows));
}

if ($method === 'GET' && preg_match('#^/articles/(\d+)$#', $path, $m)) {
    $st = $pdo->prepare('SELECT * FROM articles WHERE id = ?');
    $st->execute([$m[1]]);
    $row = $st->fetch(PDO::FETCH_ASSOC);
    if (!$row) send_json(['error' => 'not found'], 404);
    send_json(article_json($row));
}

if ($method === 'POST' && $path === '/articles') {
    $headers = function_exists('getallheaders') ? array_change_key_case(getallheaders()) : [];
    $auth = $headers['authorization'] ?? ($_SERVER['HTTP_AUTHORIZATION'] ?? '');
    if ($auth !== 'secret-token') send_json(['error' => 'unauthorized'], 401);

    parse_str(file_get_contents('php://input'), $form);
    $title = $form['title'] ?? '';
    if ($title === '') send_json(['error' => 'title is required'], 422);
    $views = (int) ($form['views'] ?? 0);
    $published = in_array($form['published'] ?? '', ['1', 'true'], true) ? 1 : 0;

    $st = $pdo->prepare('INSERT INTO articles (title, views, published) VALUES (?, ?, ?)');
    $st->execute([$title, $views, $published]);
    send_json(['id' => (int) $pdo->lastInsertId(), 'title' => $title,
               'views' => $views, 'published' => (bool) $published], 201);
}

http_response_code(404);
header('Content-Type: text/plain');
echo 'Not Found';
