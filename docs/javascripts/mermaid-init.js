let rerenderCounter = 0;

async function renderMermaidDiagrams() {
  mermaid.initialize({
    startOnLoad: false,
    securityLevel: "loose",
    theme: document.body.dataset.mdColorScheme === "slate" ? "dark" : "default",
  });

  const sourceBlocks = document.querySelectorAll(".mermaid-source");
  for (const [index, sourceBlock] of sourceBlocks.entries()) {
    const source = sourceBlock.textContent ?? "";
    if (!source.trim()) {
      continue;
    }

    const output = document.createElement("div");
    output.className = "mermaid-diagram";
    output.dataset.mermaidSource = source;
    output.dataset.mermaidIndex = String(index);

    try {
      const { svg, bindFunctions } = await mermaid.render(`mermaid-diagram-${index}`, source);
      output.innerHTML = svg;
      if (typeof bindFunctions === "function") {
        bindFunctions(output);
      }
      sourceBlock.replaceWith(output);
    } catch {
      // Leave the source block in place if Mermaid fails so the page still shows the diagram text.
    }
  }
}

async function rerenderMermaidDiagrams() {
  mermaid.initialize({
    startOnLoad: false,
    securityLevel: "loose",
    theme: document.body.dataset.mdColorScheme === "slate" ? "dark" : "default",
  });

  const diagrams = document.querySelectorAll(".mermaid-diagram");
  for (const diagram of diagrams) {
    const source = diagram.dataset.mermaidSource;
    const index = diagram.dataset.mermaidIndex;
    if (!source) {
      continue;
    }

    try {
      const id = `mermaid-diagram-rerender-${rerenderCounter++}-${index}`;
      const { svg, bindFunctions } = await mermaid.render(id, source);
      diagram.innerHTML = svg;
      if (typeof bindFunctions === "function") {
        bindFunctions(diagram);
      }
    } catch {
      // Keep existing diagram if re-render fails.
    }
  }
}

new MutationObserver((mutations) => {
  for (const mutation of mutations) {
    if (mutation.attributeName === "data-md-color-scheme") {
      rerenderMermaidDiagrams();
      break;
    }
  }
}).observe(document.body, { attributes: true });

if (document.readyState === "complete") {
  renderMermaidDiagrams();
} else {
  window.addEventListener("load", () => {
    renderMermaidDiagrams();
  }, { once: true });
}