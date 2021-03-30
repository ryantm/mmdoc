function setupSearch() {
  const fuse = new Fuse(corpus, { keys: ['url', 'text'] })
  let input = document.getElementById('search')
  let timeout = null
  input.addEventListener('keyup', function (e) {
    if (e.key === "Escape") {
      toggleSearch()
      return;
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

function toggleSidebar() {
  document.querySelector('html').classList.toggle("sidebar-hidden")
  if (document.querySelector('html').classList.contains("sidebar-hidden"))
    localStorage.setItem('sidebar-hidden', '');
  else
    localStorage.removeItem('sidebar-hidden', '');
}

function setupToggleSidebar() {
  Array.from(document.querySelectorAll('.sidebar-toggle')).forEach(el => {
    el.addEventListener('click', toggleSidebar)
  })
}

window.addEventListener('DOMContentLoaded', setupToggleSidebar)

function toggleSearch() {
  document.querySelector('html').classList.toggle("search-visible")
  document.getElementById('search').select();
}

function setupToggleSearch() {
  Array.from(document.querySelectorAll('.search-toggle')).forEach(el => {
    el.addEventListener('click', toggleSearch)
  })
}

window.addEventListener('DOMContentLoaded', setupToggleSearch)
