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

window.addEventListener('DOMContentLoaded', setupSaveSidebar)

if (getSidebarClosed())
  showSidebar()
else
  hideSidebar()

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

window.addEventListener('DOMContentLoaded', setupToggleTheme)

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

