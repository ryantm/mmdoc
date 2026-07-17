//// Search

const searchResultLimit = 50
let searchIndex = null
let searchLoadPromise = null
let searchPreviousFocus = null

function loadSearchScript(source) {
  return new Promise((resolve, reject) => {
    const script = document.createElement('script')
    script.src = source
    script.addEventListener('load', resolve, { once: true })
    script.addEventListener('error', reject, { once: true })
    document.head.appendChild(script)
  })
}

function loadSearchAssets() {
  if (searchLoadPromise !== null)
    return searchLoadPromise

  const statusElem = document.getElementById('search-status')
  statusElem.textContent = 'Loading search'
  searchLoadPromise = Promise.all([
    loadSearchScript('search_index.js'),
    loadSearchScript('fuse.basic.min.js')
  ]).then(() => {
    const searchCorpus = typeof corpus === 'undefined'
      ? window.mmdocSearchCorpus
      : corpus
    if (!Array.isArray(searchCorpus))
      throw new Error('Search corpus did not load')
    searchIndex = new Fuse(searchCorpus, {
      keys: ['title', 'text'],
      ignoreLocation: true,
      threshold: 0.01,
      minMatchCharLength: 3
    })
    if (document.getElementById('search').value === '')
      statusElem.textContent = ''
    return true
  }).catch(() => {
    statusElem.textContent = 'Search is unavailable'
    return false
  })
  return searchLoadPromise
}

function setupSearch() {
  const input = document.getElementById('search')
  const resultsElem = document.getElementById('search-results')
  const statusElem = document.getElementById('search-status')
  let timeout = null

  function runSearch() {
    resultsElem.replaceChildren()
    statusElem.textContent = ''
    if (input.value === '' || searchIndex === null)
      return

    const results = searchIndex.search(
      input.value, { limit: searchResultLimit })
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
  }

  input.addEventListener('input', function () {
    clearTimeout(timeout)
    if (input.value === '') {
      resultsElem.replaceChildren()
      statusElem.textContent = ''
      return
    }
    loadSearchAssets().then(loaded => {
      if (!loaded)
        return
      clearTimeout(timeout)
      timeout = setTimeout(runSearch, 100)
    })
  })
}

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
    loadSearchAssets()
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
