export const VIEWS = ['node', 'game', 'edit'];
export function initialState() { return { visible: ['node'], capacity: 2, pinned: [], focused: 'node', recent: ['node'] }; }
export function focusView(state, view) {
  if (!state.visible.includes(view)) return state;
  return { ...state, focused: view, recent: [...state.recent.filter(v => v !== view), view] };
}
export function openView(state, view) {
  if (!VIEWS.includes(view)) return { state };
  if (state.visible.includes(view)) return { state: focusView(state, view) };
  let visible = [...state.visible];
  if (visible.length >= state.capacity) {
    const replace = state.recent.find(v => visible.includes(v) && !state.pinned.includes(v));
    if (!replace) return { state, message: 'Yeni panel için bir sabitlemeyi kaldır.' };
    visible[visible.indexOf(replace)] = view;
  } else visible.push(view);
  return { state: focusView({ ...state, visible }, view) };
}
export function closeView(state, view) {
  if (!state.visible.includes(view)) return { state };
  const visible = state.visible.filter(v => v !== view);
  const recent = state.recent.filter(v => v !== view);
  const focused = visible.includes(state.focused) ? state.focused : [...recent].reverse().find(v => visible.includes(v)) ?? null;
  return { state: { ...state, visible, recent, focused, pinned: state.pinned.filter(v => v !== view) } };
}
export function togglePin(state, view) {
  if (!state.visible.includes(view)) return { state };
  const pinned = state.pinned.includes(view) ? state.pinned.filter(v => v !== view) : [...state.pinned, view];
  return { state: focusView({ ...state, pinned }, view) };
}
export function setCapacity(state, capacity) {
  if (![2, 3].includes(capacity) || capacity === state.capacity) return { state };
  if (state.pinned.length > capacity) return { state, message: 'İki panele geçmek için bir sabitlemeyi kaldır.' };
  if (capacity === 3) {
    const focused = state.focused ?? 'node';
    return { state: { ...state, capacity, visible: [...VIEWS], focused, recent: [...VIEWS.filter(v => v !== focused), focused] } };
  }
  let visible = [...state.visible];
  while (visible.length > capacity) {
    const remove = state.recent.find(v => visible.includes(v) && !state.pinned.includes(v));
    visible = visible.filter(v => v !== remove);
  }
  const focused = visible.includes(state.focused) ? state.focused : [...state.recent].reverse().find(v => visible.includes(v)) ?? null;
  return { state: { ...state, capacity, visible, focused } };
}
