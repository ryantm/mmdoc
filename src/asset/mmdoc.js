//// Sidebar
function getSidebarClosed() {
  return (localStorage.getItem('sidebar-closed') ?? 'false') === 'true'
}

function showSidebar() {
  document.getElementById('sidebar-checkbox').checked = true
}

function hideSidebar() {
  document.getElementById('sidebar-checkbox').checked = false
}

function saveSidebar() {
  const sidebarClosed = document.getElementById('sidebar-checkbox').checked
  localStorage.setItem('sidebar-closed', sidebarClosed)
}

function setupSaveSidebar() {
  const el = document.getElementById('sidebar-checkbox')
  el.addEventListener('change', saveSidebar)
}

setupSaveSidebar()

if (getSidebarClosed())
  showSidebar()
else
  hideSidebar()

function normalizedPagePath(pathname) {
  const withoutIndex = pathname.replace(/\/index\.html?$/i, '')
  return withoutIndex.replace(/\/$/, '') || '/'
}

function scrollSidebarToLink(sidebar, link) {
  window.requestAnimationFrame(() => {
    const sidebarRect = sidebar.getBoundingClientRect()
    if (sidebarRect.height === 0)
      return

    const linkRect = link.getBoundingClientRect()
    const offset = linkRect.top + linkRect.height / 2 -
      (sidebarRect.top + sidebarRect.height / 2)

    sidebar.scrollTop += offset
  })
}

function showCurrentPageInSidebar() {
  const sidebar = document.querySelector('nav.sidebar')
  if (sidebar === null)
    return

  const currentPath = normalizedPagePath(window.location.pathname)
  const samePageLinks = Array.from(sidebar.querySelectorAll('a[href]')).filter(
    link => {
      const url = new URL(link.href, window.location.href)
      return url.origin === window.location.origin &&
        normalizedPagePath(url.pathname) === currentPath
    })

  if (samePageLinks.length === 0)
    return

  const matchingHashLink = samePageLinks.find(link => {
    const url = new URL(link.href, window.location.href)
    return url.hash !== '' && url.hash === window.location.hash
  })
  const currentPageLink = matchingHashLink ?? samePageLinks[0]

  sidebar.querySelectorAll('a[aria-current="page"]').forEach(link => {
    link.removeAttribute('aria-current')
  })
  currentPageLink.setAttribute('aria-current', 'page')
  scrollSidebarToLink(sidebar, currentPageLink)
}

showCurrentPageInSidebar()
window.addEventListener('hashchange', showCurrentPageInSidebar)
document.getElementById('sidebar-checkbox').addEventListener(
  'change', showCurrentPageInSidebar)

//// Theme

// Returns 'light' or 'dark'
function getDefaultTheme() {
  if (window.matchMedia('(prefers-color-scheme: dark)').matches)
    return 'dark'
  else
    return 'light'
}

// Returns 'light' or 'dark'
function getSelectedTheme() {
  return localStorage.getItem('theme') ?? getDefaultTheme()
}

function toggleTheme() {
  const theme = getSelectedTheme()

  if (theme === 'dark') {
    localStorage.setItem('theme', 'light')
    setLightTheme()
  } else if (theme === 'light') {
    localStorage.setItem('theme', 'dark')
    setDarkTheme()
  }
}

function setDarkTheme() {
  document.querySelector('html').classList.remove("light-theme")
  document.querySelector('html').classList.add("dark-theme")
}

function setLightTheme() {
  document.querySelector('html').classList.remove("dark-theme")
  document.querySelector('html').classList.add("light-theme")
}

function setupToggleTheme() {
  Array.from(document.querySelectorAll('.theme-toggle')).forEach(el => {
    el.addEventListener('click', toggleTheme)
  })
}

setupToggleTheme()

if (getSelectedTheme() === 'dark')
  setDarkTheme();
else
  setLightTheme();

// Highlight
window.addEventListener('load', (event) => {
  let codeElems =
      Array.from(document.querySelectorAll('code')).filter(
        function (elem) {
          return !elem.parentElement.classList.contains('heade')
        })
  codeElems.forEach(e => e.classList.add('hljs'))
})

window.addEventListener('keydown', (event) => {
  e = event || window.event;

  var button = null
  if (e.key === 'ArrowLeft') {
    button = document.getElementById('chapter-previous-button')
  }
  else if (e.key === 'ArrowRight') {
    button = document.getElementById('chapter-next-button')
  }
  if (button !== null) {
    button.click()
  }

})
