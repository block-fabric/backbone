// The frame of the window drawn by the page, in the colours of the theme (ThemedFrame of chains-qt): the
// title bar holds the menus, the title and the buttons of the window; the border resizes it. The host
// takes the frame of the desktop away (window.state says whether it did) and moves or resizes the
// window when the page asks, while the button of the pointer is down.
import { on, request } from './bridge';

export const frame = $state({ framed: false, maximized: false, title: '' });

export async function loadFrame(): Promise<void> {
  try {
    Object.assign(frame, await request<{ framed: boolean; maximized: boolean; title: string }>('host', 'window.state', {}));
  } catch {
    frame.framed = false;
  }
}

let following = false;
export function followFrame(): void {
  if (following) return;
  following = true;
  on<{ framed?: boolean; maximized?: boolean; title?: string }>('window', (data) => {
    if (typeof data.framed === 'boolean') frame.framed = data.framed;
    if (typeof data.maximized === 'boolean') frame.maximized = data.maximized;
    if (typeof data.title === 'string') frame.title = data.title;
  });
}

/** Whether a press on this element drags the window: the bar itself, not a control on it. */
function onBar(target: EventTarget | null): boolean {
  const el = target as HTMLElement | null;
  return !!el && !el.closest('button, a, input, select, textarea, [role="menu"], [role="menubar"] > *');
}

/** A press on the title bar: the window follows the pointer; twice: maximized, or back. */
export function barMouseDown(e: MouseEvent): void {
  if (!frame.framed || e.button !== 0 || !onBar(e.target)) return;
  e.preventDefault();
  if (e.detail >= 2) {
    request('host', 'window.toggleMaximized', {}).catch(() => undefined);
    return;
  }
  request('host', 'window.beginMove', {}).catch(() => undefined);
}

export function resizeFrom(edge: string, e: MouseEvent): void {
  if (!frame.framed || e.button !== 0) return;
  e.preventDefault();
  request('host', 'window.beginResize', { edge }).catch(() => undefined);
}

export const minimize = () => request('host', 'window.minimize', {}).catch(() => undefined);
export const toggleMaximized = () => request('host', 'window.toggleMaximized', {}).catch(() => undefined);
export const closeWindow = () => request('host', 'window.close', {}).catch(() => undefined);
