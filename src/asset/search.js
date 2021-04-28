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
function toggleTheme() {
  const theme = localStorage.getItem('theme')
  if (theme === 'dark')
    setLightTheme()
  else if (theme === 'light')
    setDarkTheme()
}

function setDarkTheme() {
  var dark_css = document.querySelector("link[href='a11y-dark.css']")
  var light_css = document.querySelector("link[href='a11y-light.css']")
  dark_css.setAttribute("media", "")
  light_css.setAttribute("media", "false")
  localStorage.setItem('theme', 'dark')
  document.querySelector('html').classList.remove("light-theme")
  document.querySelector('html').classList.add("dark-theme")
}

function setLightTheme() {
  var dark_css = document.querySelector("link[href='a11y-dark.css']")
  var light_css = document.querySelector("link[href='a11y-light.css']")
  dark_css.setAttribute("media", "false")
  light_css.setAttribute("media", "")
  localStorage.setItem('theme', 'light')
  document.querySelector('html').classList.remove("dark-theme")
  document.querySelector('html').classList.add("light-theme")
}

function setupToggleTheme() {
  Array.from(document.querySelectorAll('.theme-toggle')).forEach(el => {
    el.addEventListener('click', toggleTheme)
  })
}

window.addEventListener('DOMContentLoaded', setupToggleTheme)

if (localStorage.getItem('theme') === 'dark')
  setDarkTheme();
if (localStorage.getItem('theme') === 'light')
  setLightTheme();
