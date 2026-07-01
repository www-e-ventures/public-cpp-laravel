// Fetch the JSON API that the cpp-laravel app serves and show it. This file is
// served from /assets/app.js with Content-Type: application/javascript.
fetch('/api/items')
  .then((r) => r.json())
  .then((items) => {
    document.getElementById('out').textContent =
      'items from cpp-laravel:\n' + JSON.stringify(items, null, 2);
  })
  .catch((e) => {
    document.getElementById('out').textContent = 'error: ' + e;
  });
