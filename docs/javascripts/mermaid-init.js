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

if (document.readyState === "complete") {
  renderMermaidDiagrams();
} else {
  window.addEventListener("load", () => {
    renderMermaidDiagrams();
  }, { once: true });
}