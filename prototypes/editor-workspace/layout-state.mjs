let nextId = 0;
const leaf = view => ({ type: 'leaf', view });
const split = (a, b, axis = 'x') => ({ type: 'split', id: `split-${++nextId}`, axis, ratio: .5, a, b });
export const initialLayout = () => ({ tree: leaf('node') });
export function leaves(tree) {
  return !tree ? [] : tree.type === 'leaf' ? [tree.view] : [...leaves(tree.a), ...leaves(tree.b)];
}
export const layoutViews = layout => leaves(layout.tree);
function mapLeaf(tree, view, transform) {
  if (!tree) return null;
  if (tree.type === 'leaf') return tree.view === view ? transform(tree) : tree;
  const a = mapLeaf(tree.a, view, transform), b = mapLeaf(tree.b, view, transform);
  return !a ? b : !b ? a : { ...tree, a, b };
}
export function removeView(layout, view) {
  return { tree: mapLeaf(layout.tree, view, () => null) };
}
export function renameView(layout, oldView, newView) {
  return { tree: mapLeaf(layout.tree, oldView, () => leaf(newView)) };
}
export function dockView(layout, source, target, edge) {
  const views = layoutViews(layout);
  if (source === target || !views.includes(source) || !views.includes(target) || !['left', 'right', 'top', 'bottom'].includes(edge)) return layout;
  let next = removeView(layout, source);
  const before = ['left', 'top'].includes(edge), axis = ['left', 'right'].includes(edge) ? 'x' : 'y';
  return { ...next, tree: mapLeaf(next.tree, target, node => split(before ? leaf(source) : node, before ? node : leaf(source), axis)) };
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
  // Replacement inherits the existing split area.
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
