export const CORE_VIEWS = ['node', 'game', 'edit'];
export const TOOL_VIEWS = ['hierarchy', 'inspector', 'assets', 'console', 'lua'];
export const VIEWS = [...CORE_VIEWS, ...TOOL_VIEWS];
export function initialState() { return { visible: ['node'], pinned: [], focused: 'node', recent: ['node'] }; }
export function focusView(state, view) {
  if (!state.visible.includes(view)) return state;
  return { ...state, focused: view, recent: [...state.recent.filter(v => v !== view), view] };
}
export function openView(state, view) {
  if (!VIEWS.includes(view)) return { state };
  if (state.visible.includes(view)) return { state: focusView(state, view) };
  return { state: focusView({ ...state, visible: [...state.visible, view] }, view) };
}
export function closeView(state, view) {
  if (!state.visible.includes(view)) return { state };
  const visible = state.visible.filter(v => v !== view);
  const recent = state.recent.filter(v => v !== view);
  const focused = visible.includes(state.focused) ? state.focused : [...recent].reverse().find(v => visible.includes(v)) ?? null;
  return { state: { ...state, visible, recent, focused, pinned: state.pinned.filter(v => v !== view) } };
}
export function toggleView(state, view) {
  return state.visible.includes(view) ? closeView(state, view) : openView(state, view);
}
export function togglePin(state, view) {
  if (!state.visible.includes(view)) return { state };
  const pinned = state.pinned.includes(view) ? state.pinned.filter(v => v !== view) : [...state.pinned, view];
  return { state: focusView({ ...state, pinned }, view) };
}
