"use strict";
const search = document.getElementById("search");
const links = [...document.querySelectorAll("nav a")];
const status = document.getElementById("search-status");
function filter() {
  const query = search.value.trim().toLocaleLowerCase("ko");
  let count = 0;
  for (const link of links) {
    link.hidden = !(`${link.textContent} ${link.dataset.keywords}`.toLocaleLowerCase("ko").includes(query));
    if (!link.hidden) ++count;
  }
  status.textContent = query ? `${count}개 작업${count ? "" : " · 다른 검색어를 입력하세요"}` : "";
  const url = new URL(location.href);
  if (query) url.searchParams.set("q", search.value); else url.searchParams.delete("q");
  history.replaceState(null, "", url);
}
function markCurrent() {
  for (const link of links) {
    if (link.hash === (location.hash || "#start")) link.setAttribute("aria-current", "location");
    else link.removeAttribute("aria-current");
  }
}
search.value = new URL(location.href).searchParams.get("q") || "";
search.addEventListener("input", filter);
window.addEventListener("hashchange", markCurrent);
filter(); markCurrent();
