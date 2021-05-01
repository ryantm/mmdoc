function setupSearch() {
  const fuse = new Fuse(corpus, { keys: ['url', 'text'] })
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

function toggleSidebar() {
  document.querySelector('html').classList.toggle("sidebar-hidden")
  if (document.querySelector('html').classList.contains("sidebar-hidden"))
    localStorage.setItem('sidebar-hidden', '')
  else
    localStorage.removeItem('sidebar-hidden', '')
}

function setupToggleSidebar() {
  Array.from(document.querySelectorAll('.sidebar-toggle')).forEach(el => {
    el.addEventListener('click', toggleSidebar)
  })
}

window.addEventListener('DOMContentLoaded', setupToggleSidebar)

if (localStorage.getItem('sidebar-hidden') === '')
  document.querySelector('html').classList.add('sidebar-hidden')


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

//Theme

// Returns 'light' or 'dark'
function getSystemPreferredTheme() {
  if (window.matchMedia('(prefers-color-scheme: dark)').matches)
    return 'dark'
  else
    return 'light'
}

// Returns 'light' or 'dark'
function getSelectedTheme() {
  return localStorage.getItem('theme') ?? getSystemPreferredTheme()
}

function toggleTheme() {
  const theme = getSelectedTheme();

  if (theme === 'dark')
  {
    localStorage.setItem('theme', 'light')
    setLightTheme()
  }
  else if (theme === 'light')
  {
    localStorage.setItem('theme', 'dark')
    setDarkTheme()
  }
}

function setDarkTheme() {
  var dark_css = document.querySelector("link[href='a11y-dark.css']")
  var light_css = document.querySelector("link[href='a11y-light.css']")
  dark_css.setAttribute("media", "")
  light_css.setAttribute("media", "false")
  document.querySelector('html').classList.remove("light-theme")
  document.querySelector('html').classList.add("dark-theme")
}

function setLightTheme() {
  var dark_css = document.querySelector("link[href='a11y-dark.css']")
  var light_css = document.querySelector("link[href='a11y-light.css']")
  dark_css.setAttribute("media", "false")
  light_css.setAttribute("media", "")
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
