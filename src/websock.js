const elements = domLoaded.then(() => {
    const ans_cont = document.getElementById("answers")
    return { ans_cont }
})

const socket = new WebSocket("ws://localhost:1619/ws");
socket.binaryType = 'arraybuffer' // why?

socket.addEventListener("open", (event) => {
    console.log("Connection opened!");
});

socket.addEventListener("message", (event) => {
    const fields = ["", "url", "name", "desc"]

    console.log("Message");

    const td = new TextDecoder('utf8')
    const dv = new Uint8Array(event.data)

    var answers = []
    var ans = {}
    var off = 0
    while(off < dv.length) {
        const tag = dv[off]

        off++
        if(tag == 5) {
            const root = document.createElement('div')
            root.innerText = "None!"

            ans_cont.append(root);
        }
        else if(tag == 4) {
            const root = document.createElement('a')
            root.setAttribute('href', ans.url)
            root.classList.add('ans')
            root.setAttribute('target', '__blank')

            const url = document.createElement('div')
            url.innerHTML = ans.url

            const name = document.createElement('div')
            name.classList.add('name')
            name.innerHTML = ans.name

            const desc = document.createElement('div')
            desc.innerHTML = ans.desc
            root.append(url, name, desc)

            answers.push(root)
        }
        else {
            const size = dv[off]
            off++
            const str = td.decode(dv.subarray(off, off + size))
            ans[fields[tag]] = str
            off += size
        }
    }

    elements.then((els) => {
        els.ans_cont.append(...answers)
    })
});
