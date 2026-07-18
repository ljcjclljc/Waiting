(() => {
  const root = document.documentElement;
  const storedTheme = localStorage.getItem("blog-theme");
  const preferredDark = window.matchMedia("(prefers-color-scheme: dark)").matches;
  root.dataset.theme = storedTheme || (preferredDark ? "dark" : "light");

  const syncCommentTheme = () => {
    const frame = document.querySelector("iframe.giscus-frame");
    frame?.contentWindow?.postMessage(
      { giscus: { setConfig: { theme: root.dataset.theme } } },
      "https://giscus.app",
    );
  };

  window.addEventListener("message", (event) => {
    const frame = document.querySelector("iframe.giscus-frame");
    const height = Number(event.data?.giscus?.resizeHeight);
    if (
      event.origin === "https://giscus.app" &&
      frame &&
      Number.isFinite(height) &&
      height >= 150 &&
      height <= 10000
    ) {
      frame.style.height = `${height}px`;
    }
  });

  const initializeUi = () => {
    document.querySelector(".theme-toggle")?.addEventListener("click", () => {
      root.dataset.theme = root.dataset.theme === "dark" ? "light" : "dark";
      localStorage.setItem("blog-theme", root.dataset.theme);
      syncCommentTheme();
    });

    const navToggle = document.querySelector(".nav-toggle");
    const nav = document.querySelector(".main-nav");
    navToggle?.addEventListener("click", () => {
      const open = nav?.classList.toggle("is-open") ?? false;
      navToggle.setAttribute("aria-expanded", String(open));
    });

    if (document.querySelector('script[src="https://giscus.app/client.js"]')) {
      const commentObserver = new MutationObserver(() => {
        const frame = document.querySelector("iframe.giscus-frame");
        if (frame) {
          frame.addEventListener("load", syncCommentTheme, { once: true });
          commentObserver.disconnect();
        }
      });
      commentObserver.observe(document.body, { childList: true, subtree: true });
    }
  };

  if (document.readyState === "loading")
    document.addEventListener("DOMContentLoaded", initializeUi, { once: true });
  else initializeUi();
})();
