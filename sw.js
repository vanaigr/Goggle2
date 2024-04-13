self.addEventListener('install', () => {
    self.skipWaiting()
    console.log('!!!!')
})

self.addEventListener("activate", (event) => {
  event.waitUntil(clients.claim());
});

self.addEventListener("fetch", (event) => {
    console.log('fetch')
  event.respondWith(new Response(
      '<html><head></head><body><p>Hello from service worker!</p>'
      //+ '<script>navigator.serviceWorker.register("/sw.js", {scope: "/"});</script>'
      + '</body></html>',
      { headers: { 'Content-Type': 'text/html' } }
  ));
});
