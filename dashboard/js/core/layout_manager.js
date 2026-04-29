export class LayoutManager {
  constructor(container, registry, context) {
    this.container = container;
    this.registry = registry;
    this.context = context;
    this.grid = null;
    this.panelInstances = new Map();
  }

  mount() {
    this.grid = GridStack.init(
      {
        column: 12,
        cellHeight: 88,
        margin: 12,
        float: true,
        handle: ".panel-drag-handle",
        resizable: { handles: "e, se, s, sw, w" }
      },
      this.container
    );

    for (const definition of this.registry.list()) {
      const node = this.createPanelNode(definition);
      this.container.appendChild(node.item);
      this.grid.makeWidget(node.item);
      const instance = definition.mount(node.body, this.context);
      this.panelInstances.set(definition.id, instance);
    }

    this.grid.on("resizestop", (_event, element) => {
      const id = element.getAttribute("gs-id");
      this.panelInstances.get(id)?.resize?.();
    });

    window.addEventListener("resize", () => {
      this.panelInstances.forEach((instance) => instance.resize?.());
    });
  }

  getPanel(id) {
    return this.panelInstances.get(id);
  }

  createPanelNode(definition) {
    const item = document.createElement("section");
    item.className = "grid-stack-item";
    item.setAttribute("gs-id", definition.id);
    item.setAttribute("gs-x", String(definition.layout.x));
    item.setAttribute("gs-y", String(definition.layout.y));
    item.setAttribute("gs-w", String(definition.layout.w));
    item.setAttribute("gs-h", String(definition.layout.h));
    item.setAttribute("aria-label", definition.title);

    const content = document.createElement("div");
    content.className = "grid-stack-item-content panel-shell";

    const header = document.createElement("header");
    header.className = "panel-shell-header panel-drag-handle";

    const titleBlock = document.createElement("div");
    titleBlock.className = "title-block";
    const title = document.createElement("h2");
    title.className = "panel-title";
    title.textContent = definition.title;
    const subtitle = document.createElement("span");
    subtitle.className = "panel-subtitle";
    subtitle.textContent = definition.subtitle || "";
    titleBlock.append(title, subtitle);

    const actions = document.createElement("div");
    actions.className = "panel-actions";
    header.append(titleBlock, actions);

    const body = document.createElement("div");
    body.className = `panel-body panel-body-${definition.id}`;
    body.dataset.panelId = definition.id;

    content.append(header, body);
    item.appendChild(content);
    return { item, body, actions };
  }
}
