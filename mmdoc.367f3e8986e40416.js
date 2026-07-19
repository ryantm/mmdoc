//// Sidebar
const sidebarMedia = window.matchMedia('(max-width: 700px)')

function getSidebarClosed() {
  return (localStorage.getItem('sidebar-closed') ?? 'false') === 'true'
}

function isSidebarVisible() {
  const checked = document.getElementById('sidebar-checkbox').checked
  return sidebarMedia.matches ? checked : !checked
}

function updateSidebarToggle(visible) {
  document.querySelectorAll('.sidebar-toggle').forEach(button => {
    button.setAttribute('aria-expanded', String(visible))
    button.setAttribute(
      'aria-label', visible ? 'Hide table of contents' : 'Show table of contents')
  })
}

function setSidebarVisible(visible) {
  document.getElementById('sidebar-checkbox').checked =
    sidebarMedia.matches ? visible : !visible
  updateSidebarToggle(visible)
}

function toggleSidebar() {
  setSidebarVisible(!isSidebarVisible())
  if (!sidebarMedia.matches)
    localStorage.setItem('sidebar-closed', String(!isSidebarVisible()))
  showCurrentPageInSidebar()
}

function closeSidebarOnMobileNavigation() {
  if (!sidebarMedia.matches)
    return

  setSidebarVisible(false)
}

function setupSidebar() {
  document.querySelectorAll('.sidebar-toggle').forEach(button => {
    button.addEventListener('click', toggleSidebar)
  })
  Array.from(document.querySelectorAll('nav.sidebar a[href]')).forEach(link => {
    link.addEventListener('click', closeSidebarOnMobileNavigation)
  })
  sidebarMedia.addEventListener('change', event => {
    setSidebarVisible(event.matches ? false : !getSidebarClosed())
  })
  setSidebarVisible(sidebarMedia.matches ? false : !getSidebarClosed())
}

setupSidebar()

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
  let theme = null
  try { theme = localStorage.getItem('theme') } catch (error) {}
  return theme === 'light' || theme === 'dark' ? theme : getDefaultTheme()
}

function saveSelectedTheme(theme) {
  try { localStorage.setItem('theme', theme) } catch (error) {}
}

function toggleTheme() {
  const theme = getSelectedTheme()

  if (theme === 'dark') {
    saveSelectedTheme('light')
    setLightTheme()
  } else if (theme === 'light') {
    saveSelectedTheme('dark')
    setDarkTheme()
  }
}

function setDarkTheme() {
  document.querySelector('html').classList.remove("light-theme")
  document.querySelector('html').classList.add("dark-theme")
  updateThemeToggle('dark')
}

function setLightTheme() {
  document.querySelector('html').classList.remove("dark-theme")
  document.querySelector('html').classList.add("light-theme")
  updateThemeToggle('light')
}

function updateThemeToggle(theme) {
  const dark = theme === 'dark'
  document.querySelectorAll('.theme-toggle').forEach(button => {
    button.setAttribute('aria-pressed', String(dark))
    button.setAttribute(
      'aria-label', dark ? 'Switch to light theme' : 'Switch to dark theme')
  })
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

if (typeof hljs !== 'undefined')
  hljs.highlightAll()

function isInteractiveTarget(target) {
  return target.closest(
    'a, button, input, select, textarea, [contenteditable="true"]') !== null
}

window.addEventListener('keydown', (event) => {
  if (event.defaultPrevented || event.altKey || event.ctrlKey || event.metaKey ||
      event.shiftKey || isInteractiveTarget(event.target))
    return

  let button = null
  if (event.key === 'ArrowLeft') {
    button = document.getElementById('chapter-previous-button')
  }
  else if (event.key === 'ArrowRight') {
    button = document.getElementById('chapter-next-button')
  }
  if (button !== null) {
    event.preventDefault()
    button.click()
  }
})
