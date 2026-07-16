//// Search

function setupSearch() {
  const fuse = new Fuse(corpus, { keys: ['title', 'text'], ignoreLocation: true, threshold: 0.01, minMatchCharLength: 3 })
  const input = document.getElementById('search')
  const resultsElem = document.getElementById('search-results')
  const statusElem = document.getElementById('search-status')
  let timeout = null
  input.addEventListener('input', function () {
    clearTimeout(timeout)
    timeout = setTimeout(function () {
      resultsElem.innerHTML = ""
      statusElem.textContent = ""
      if (input.value === "")
        return
      const results = fuse.search(input.value)
      const resultLabel = results.length === 1 ? 'search result' : 'search results'
      statusElem.textContent = results.length + ' ' + resultLabel
      const h2 = document.createElement('h2')
      h2.appendChild(document.createTextNode(results.length + ' ' + resultLabel))
      resultsElem.appendChild(h2)
      const ol = document.createElement('ol')
      resultsElem.appendChild(ol)
      results.forEach(function (result) {
        const li = document.createElement('li')
        const a = document.createElement('a')
        a.href = result.item.url
        a.appendChild(document.createTextNode(result.item.title))
        li.appendChild(a)
        ol.appendChild(li)
      })
    }, 100)
  })
}

let searchPreviousFocus = null

function moveSearchResultFocus(direction) {
  const input = document.getElementById('search')
  const links = Array.from(
    document.querySelectorAll('#search-results a[href]'))
  if (links.length === 0)
    return

  const currentIndex = links.indexOf(document.activeElement)
  if (currentIndex === -1) {
    links[direction > 0 ? 0 : links.length - 1].focus()
    return
  }

  const nextIndex = currentIndex + direction
  if (nextIndex < 0 || nextIndex >= links.length)
    input.focus()
  else
    links[nextIndex].focus()
}

function setSearchVisible(visible) {
  const root = document.querySelector('html')
  const panel = document.getElementById('search-panel')
  const input = document.getElementById('search')
  root.classList.toggle('search-visible', visible)
  panel.hidden = !visible
  document.querySelectorAll('.search-toggle').forEach(button => {
    button.setAttribute('aria-expanded', String(visible))
    button.setAttribute('aria-label', visible ? 'Close search' : 'Open search')
  })

  if (visible) {
    searchPreviousFocus = document.activeElement
    if (typeof setSidebarVisible === 'function' && sidebarMedia.matches)
      setSidebarVisible(false)
    input.focus()
    input.select()
  } else if (searchPreviousFocus !== null) {
    searchPreviousFocus.focus()
    searchPreviousFocus = null
  }
}

function toggleSearch() {
  const visible = document.getElementById('search-panel').hidden
  setSearchVisible(visible)
}

function setupToggleSearch() {
  Array.from(document.querySelectorAll('.search-toggle')).forEach(el => {
    el.addEventListener('click', toggleSearch)
  })
  window.addEventListener('keydown', event => {
    const target = event.target
    const isTyping = target.closest(
      'input, select, textarea, [contenteditable="true"]') !== null
    const shortcut = event.key === '/' ||
      ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'k')
    if (event.defaultPrevented || isTyping || !shortcut)
      return
    event.preventDefault()
    setSearchVisible(true)
  })
  document.getElementById('search-panel').addEventListener('keydown', event => {
    if (event.key === 'Escape') {
      event.preventDefault()
      setSearchVisible(false)
      return
    }
    if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp')
      return
    event.preventDefault()
    moveSearchResultFocus(event.key === 'ArrowDown' ? 1 : -1)
  })
}

setupSearch()
setupToggleSearch()
