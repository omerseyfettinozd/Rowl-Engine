let nextId = 0;
const leaf = view => ({ type: 'leaf', view });
const split = (a, b, axis = 'x') => ({ type: 'split', id: `split-${++nextId}`, axis, ratio: .5, a, b });
export const initialLayout = () => ({ tree: leaf('node'), floating: {} });
export function leaves(tree) {
  return !tree ? [] : tree.type === 'leaf' ? [tree.view] : [...leaves(tree.a), ...leaves(tree.b)];
}
export const layoutViews = layout => [...leaves(layout.tree), ...Object.keys(layout.floating)];
function mapLeaf(tree, view, transform) {
  if (!tree) return null;
  if (tree.type === 'leaf') return tree.view === view ? transform(tree) : tree;
  const a = mapLeaf(tree.a, view, transform), b = mapLeaf(tree.b, view, transform);
  return !a ? b : !b ? a : { ...tree, a, b };
}
export function removeView(layout, view) {
  const floating = { ...layout.floating }; delete floating[view];
  return { tree: mapLeaf(layout.tree, view, () => null), floating };
}
export function renameView(layout, oldView, newView) {
  const floating = { ...layout.floating };
  if (floating[oldView]) { floating[newView] = floating[oldView]; delete floating[oldView]; }
  return { tree: mapLeaf(layout.tree, oldView, () => leaf(newView)), floating };
}
export function dockView(layout, source, target, edge) {
  const views = layoutViews(layout);
  if (source === target || !views.includes(source) || !views.includes(target) || layout.floating[target] || !['left', 'right', 'top', 'bottom'].includes(edge)) return layout;
  let next = removeView(layout, source);
  const before = ['left', 'top'].includes(edge), axis = ['left', 'right'].includes(edge) ? 'x' : 'y';
  return { ...next, tree: mapLeaf(next.tree, target, node => split(before ? leaf(source) : node, before ? node : leaf(source), axis)) };
}
export function dockToWorkspace(layout, view, axis = 'x') {
  if (!layoutViews(layout).includes(view)) return layout;
  const next = removeView(layout, view);
  return { ...next, tree: next.tree ? split(next.tree, leaf(view), axis) : leaf(view) };
}
export function clampRect(rect) {
  const w = Math.max(20, Math.min(100, rect.w)), h = Math.max(20, Math.min(100, rect.h));
  return { w, h, x: Math.max(0, Math.min(100 - w, rect.x)), y: Math.max(0, Math.min(100 - h, rect.y)) };
}
export function floatView(layout, view, rect = { x: 20, y: 14, w: 60, h: 65 }) {
  if (!layoutViews(layout).includes(view)) return layout;
  const next = removeView(layout, view);
  return { ...next, floating: { ...next.floating, [view]: clampRect(rect) } };
}
export function resizeSplit(layout, id, ratio) {
  function visit(tree) {
    if (!tree || tree.type === 'leaf') return tree;
    return { ...tree, ratio: tree.id === id ? Math.max(.2, Math.min(.8, ratio)) : tree.ratio, a: visit(tree.a), b: visit(tree.b) };
  }
  return { ...layout, tree: visit(layout.tree) };
}
export function syncLayout(layout, previous, next, axis = 'x') {
  let result = layout;
  const removed = previous.visible.filter(v => !next.visible.includes(v));
  const added = next.visible.filter(v => !previous.visible.includes(v));
  // Replacement inherits the exact dock slot or floating rectangle.
  while (removed.length && added.length) result = renameView(result, removed.shift(), added.shift());
  for (const view of removed) result = removeView(result, view);
  for (const view of added) {
    const tree = view === 'edit' && leaves(result.tree).includes('game')
      ? mapLeaf(result.tree, 'game', node => split(node, leaf(view), 'y'))
      : result.tree ? split(result.tree, leaf(view), axis) : leaf(view);
    result = { ...result, tree };
  }
  if (axis === 'y' && previous.visible.length === 1 && leaves(result.tree).length === 3 && result.tree?.type === 'split') {
    result = { ...result, tree: { ...result.tree, ratio: 1 / 3 } };
  }
  return result;
}
