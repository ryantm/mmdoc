//// Search

function setupSearch() {
  const fuse = new Fuse(corpus, { keys: ['title', 'text'], ignoreLocation: true, threshold: 0.01, minMatchCharLength: 3 })
  let input = document.getElementById('search')
  let timeout = null
  input.addEventListener('keyup', function (e) {
    if (e.key === "Escape") {
      toggleSearch()
      return
    }
    clearTimeout(timeout)
    timeout = setTimeout(function () {
      let resultsElem = document.getElementById('search-results')
      resultsElem.innerHTML = ""
      if (input.value === "")
        return
      let results = fuse.search(input.value)
      var h2 = document.createElement('h2')
      h2.appendChild(document.createTextNode(results.length + " search result(s)"))
      resultsElem.appendChild(h2)
      var ol = document.createElement('ol')
      resultsElem.appendChild(ol)
      results.forEach(function (result) {
        var li = document.createElement('li')
        var a = document.createElement('a')
        a.href = result.item.url
        a.appendChild(document.createTextNode(result.item.title))
        li.appendChild(a)
        ol.appendChild(li)
      })
    }, 100)
  })
}

window.addEventListener('DOMContentLoaded', setupSearch)

function toggleSearch() {
  document.querySelector('html').classList.toggle("search-visible")
  document.getElementById('search').select()
}

function setupToggleSearch() {
  Array.from(document.querySelectorAll('.search-toggle')).forEach(el => {
    el.addEventListener('click', toggleSearch)
  })
}

window.addEventListener('DOMContentLoaded', setupToggleSearch)
