(function () {
  var page = document.querySelector('.page');
  var button = document.querySelector('.menu-toggle');

  if (button && page) {
    button.addEventListener('click', function () {
      page.classList.toggle('nav-open');
    });

    document.addEventListener('click', function (event) {
      if (!page.classList.contains('nav-open')) {
        return;
      }

      var isInsideSidebar = event.target.closest('.sidebar');
      var isButton = event.target.closest('.menu-toggle');
      if (!isInsideSidebar && !isButton) {
        page.classList.remove('nav-open');
      }
    });
  }

  var links = document.querySelectorAll('.chapter-list a');
  var current = window.location.pathname.split('/').pop() || 'index.html';

  links.forEach(function (link) {
    var href = link.getAttribute('href');
    if (href === current || (href === 'index.html' && current === '')) {
      link.setAttribute('aria-current', 'page');
    }
  });
})();
