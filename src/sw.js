function html(strings, ...values) { return String.raw({ raw: strings }, ...values); }
function javascript(strings, ...values) { return String.raw({ raw: strings }, ...values); }

var page;

var socket = (() => {
    var tasks = new Map(); // url -> promise
    var when_opened = null;

    const te = new TextEncoder('utf8');
    const td = new TextDecoder('utf8');

    var s = {};
    s.setup = () => {
        if(when_opened === null) {
            console.log("Creating new websocket!");
            var websock = new WebSocket("ws://localhost:1619/ws");
            websock.binaryType = 'arraybuffer'; // why?

            var s, j;
            when_opened = new Promise((_s, _j) => { s = _s; j = _j; });
            websock.addEventListener("open", e => {
                s(websock);
            });

            websock.addEventListener("close", e => {
                when_opened = null;
                j();
                console.log("Lost " + tasks.size + " tasks");
            });

            websock.addEventListener("message", e => {
                const ab = e.data
                const array = new Uint8Array(ab)

                const tag = array[0]
                const len = array[1]
                if(tag != 1) console.error("tag? " + tag);

                const url = td.decode(array.subarray(2, 2 + len))
                console.log("Received response for " + url);
                const cb = tasks.get(url)
                cb[0](array.subarray(2 + len))
            });
        }
    };
    s.request = (url) => {
        s.setup();
        return when_opened.then((websock) => {
            var s, j;
            var p = new Promise((_s, _j) => { s = _s; j = _j; });

            tasks.set(url, [s, j])
            const url_b = te.encode(url);

            const ab = new ArrayBuffer(1 + url_b.length)
            const array = new Uint8Array(ab)
            array[0] = 1 // request url
            array.set(url_b, 1);

            websock.send(ab)
            console.log("Sent request for " + url);

            return p;
        });
    };

    return s;
})();

self.addEventListener('install', () => {
    self.skipWaiting()
    console.log('Installed');
})

self.addEventListener("activate", (event) => {
    event.waitUntil(clients.claim());
    socket.setup()
});

const p_search = /^\/search?/;

self.addEventListener("fetch", (event) => {
    const url = new URL(event.request.url);
    console.log('fetch ' + url.search);
//if(p_search.test(url)) {
    socket.request(url.search).then(() => {
        console.log("received");
    })
    event.respondWith(new Response(
        page, { headers: { 'Content-Type': 'text/html' } }
    ));
//}
});

// By default, all messages sent from a page's controlling service worker
// to the page (using Client.postMessage()) are queued while the page is loading,
// and get dispatched once the page's HTML document has been loaded and parsed
// (i.e. after the DOMContentLoaded event fires).
page = html`
<!doctype html>
<html>
    <head>
    </head>
    <body>
        Answers:
        <script>
        navigator.serviceWorker.addEventListener('message', event => {
            console.log('Message from service worker:', event.data);
        });
        </script>
    </body>
</html>
`;
