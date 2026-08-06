(() => {
  const root = document.documentElement;
  let storedTheme = null;
  try {
    storedTheme = localStorage.getItem("blog-theme");
  } catch {}
  const preferredDark = window.matchMedia("(prefers-color-scheme: dark)").matches;
  root.dataset.theme = ["light", "dark"].includes(storedTheme)
    ? storedTheme
    : preferredDark
      ? "dark"
      : "light";

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
      event.source === frame.contentWindow &&
      Number.isFinite(height) &&
      height >= 150 &&
      height <= 10000
    ) {
      frame.style.height = `${height}px`;
    }
  });

  const createToc = () => {
    const toc = document.querySelector(".article-toc");
    const headings = [...document.querySelectorAll(".prose h2, .prose h3")];
    if (!toc || headings.length === 0) return;

    headings.forEach((heading, index) => {
      if (!heading.id) heading.id = `section-${index + 1}`;
      const link = document.createElement("a");
      link.href = `#${heading.id}`;
      link.textContent = heading.textContent;
      link.dataset.level = heading.tagName.slice(1);
      toc.append(link);
    });

    const links = [...toc.querySelectorAll("a")];
    const observer = new IntersectionObserver(
      (entries) => {
        const visible = entries.find((entry) => entry.isIntersecting);
        if (!visible) return;
        links.forEach((link) =>
          link.classList.toggle("is-active", link.hash === `#${visible.target.id}`),
        );
      },
      { rootMargin: "-18% 0px -70% 0px" },
    );
    headings.forEach((heading) => observer.observe(heading));
  };

  const enhanceCodeBlocks = () => {
    document.querySelectorAll(".prose pre").forEach((pre) => {
      if (pre.parentElement?.classList.contains("code-block")) return;
      const wrapper = document.createElement("div");
      wrapper.className = "code-block";
      pre.before(wrapper);
      wrapper.append(pre);

      const button = document.createElement("button");
      button.className = "code-copy";
      button.type = "button";
      button.title = "复制代码";
      button.setAttribute("aria-label", "复制代码");
      button.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="9" y="9" width="11" height="11" rx="2"/><path d="M15 9V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v7a2 2 0 0 0 2 2h3"/></svg>';
      wrapper.append(button);

      button.addEventListener("click", async () => {
        const code = pre.querySelector("code")?.textContent ?? pre.textContent ?? "";
        try {
          if (navigator.clipboard?.writeText) {
            await navigator.clipboard.writeText(code);
          } else {
            const textarea = document.createElement("textarea");
            textarea.value = code;
            textarea.style.position = "fixed";
            textarea.style.opacity = "0";
            document.body.append(textarea);
            textarea.select();
            const copied = document.execCommand("copy");
            textarea.remove();
            if (!copied) throw new Error("Copy command failed");
          }
          button.classList.add("is-copied");
          button.title = "已复制";
          button.setAttribute("aria-label", "代码已复制");
          window.setTimeout(() => {
            button.classList.remove("is-copied");
            button.title = "复制代码";
            button.setAttribute("aria-label", "复制代码");
          }, 1600);
        } catch {
          button.title = "复制失败";
          button.setAttribute("aria-label", "代码复制失败");
        }
      });
    });
  };

  const initializeUi = () => {
    const header = document.querySelector(".site-header");
    const backgroundVideo = document.querySelector(".site-background video");
    const progress = document.querySelector(".reading-progress span");
    const backToTop = document.querySelector(".back-to-top");
    const navToggle = document.querySelector(".nav-toggle");
    const nav = document.querySelector(".main-nav");

    const currentPath = window.location.pathname;
    document.querySelectorAll(".main-nav a, .filter-rail a").forEach((link) => {
      const path = new URL(link.href, window.location.href).pathname;
      const active =
        (path === "/" && currentPath === "/") ||
        (path === "/posts" && currentPath.startsWith("/posts")) ||
        (path === "/archives" && currentPath === "/archives") ||
        (path === "/search" && currentPath === "/search") ||
        (path.startsWith("/categories/") && path === currentPath);
      if (active) link.setAttribute("aria-current", "page");
    });

    const motionPreference = window.matchMedia("(prefers-reduced-motion: reduce)");
    const syncBackgroundPlayback = () => {
      if (!backgroundVideo) return;
      if (motionPreference.matches || document.hidden) backgroundVideo.pause();
      else backgroundVideo.play().catch(() => {});
    };
    motionPreference.addEventListener?.("change", syncBackgroundPlayback);
    document.addEventListener("visibilitychange", syncBackgroundPlayback);
    syncBackgroundPlayback();

    document.querySelector(".theme-toggle")?.addEventListener("click", () => {
      root.dataset.theme = root.dataset.theme === "dark" ? "light" : "dark";
      try {
        localStorage.setItem("blog-theme", root.dataset.theme);
      } catch {}
      syncCommentTheme();
    });

    navToggle?.addEventListener("click", () => {
      const open = nav?.classList.toggle("is-open") ?? false;
      navToggle.setAttribute("aria-expanded", String(open));
      document.body.classList.toggle("nav-open", open);
    });

    nav?.querySelectorAll("a").forEach((link) => {
      link.addEventListener("click", () => {
        nav.classList.remove("is-open");
        navToggle?.setAttribute("aria-expanded", "false");
        document.body.classList.remove("nav-open");
      });
    });

    document.addEventListener("keydown", (event) => {
      if (event.key !== "Escape" || !nav?.classList.contains("is-open")) return;
      nav.classList.remove("is-open");
      navToggle?.setAttribute("aria-expanded", "false");
      document.body.classList.remove("nav-open");
      navToggle?.focus();
    });

    document.addEventListener("click", (event) => {
      if (!nav?.classList.contains("is-open")) return;
      if (nav.contains(event.target) || navToggle?.contains(event.target)) return;
      nav.classList.remove("is-open");
      navToggle?.setAttribute("aria-expanded", "false");
      document.body.classList.remove("nav-open");
    });

    const updateScrollUi = () => {
      const scrolled = window.scrollY > 24;
      header?.classList.toggle("is-scrolled", scrolled);
      backToTop?.classList.toggle("is-visible", window.scrollY > 600);
      if (progress) {
        const available = document.documentElement.scrollHeight - window.innerHeight;
        progress.style.width = `${available > 0 ? Math.min(100, (window.scrollY / available) * 100) : 0}%`;
      }
    };
    window.addEventListener("scroll", updateScrollUi, { passive: true });
    updateScrollUi();

    backToTop?.addEventListener("click", () => window.scrollTo({ top: 0, behavior: "smooth" }));

    const revealItems = [...document.querySelectorAll("[data-reveal]")];
    if (
      window.matchMedia("(prefers-reduced-motion: reduce)").matches ||
      !("IntersectionObserver" in window)
    ) {
      revealItems.forEach((item) => item.classList.add("is-visible"));
    } else {
      const revealObserver = new IntersectionObserver(
        (entries, observer) => {
          entries.forEach((entry) => {
            if (!entry.isIntersecting) return;
            entry.target.classList.add("is-visible");
            observer.unobserve(entry.target);
          });
        },
        { threshold: 0.12 },
      );
      revealItems.forEach((item) => revealObserver.observe(item));
    }

    createToc();
    enhanceCodeBlocks();

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
