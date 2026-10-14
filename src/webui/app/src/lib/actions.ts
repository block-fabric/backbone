// Svelte actions.

/**
 * Set style properties through the CSS object model. The content security policy of the app
 * allows no inline style attributes; properties set from the code are fine.
 */
export function css(node: HTMLElement, props: Record<string, string | number | null | undefined>) {
  let current: Record<string, string | number | null | undefined> = {};
  function apply(next: Record<string, string | number | null | undefined>) {
    for (const key of Object.keys(current)) {
      if (!(key in next)) node.style.removeProperty(key);
    }
    for (const [key, value] of Object.entries(next)) {
      if (value === null || value === undefined) node.style.removeProperty(key);
      else node.style.setProperty(key, String(value));
    }
    current = next;
  }
  apply(props);
  return { update: apply };
}
