import { TOOL_VIEWS } from './workspace-state.mjs';
let nextId = 0;
const leaf = view => ({ type: 'leaf', view });
const split = (a, b, axis = 'x', ratio = .5) => ({ type: 'split', id: `split-${++nextId}`, axis, ratio, a, b });
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
export function dockView(layout, source, target, edge, pinned = []) {
  const views = layoutViews(layout);
  if (pinned.includes(source) || source === target || !views.includes(source) || !views.includes(target) || !['left', 'right', 'top', 'bottom'].includes(edge)) return layout;
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
  for (const view of removed) result = removeView(result, view);
  for (const view of added) {
    let tree;
    if(view==='lua' && result.tree){
      const target=leaves(result.tree).includes('node')?'node':leaves(result.tree)[0];
      tree=mapLeaf(result.tree,target,node=>split(node,leaf(view),axis==='x'&&leaves(result.tree).length===1?'x':'y',.6));
    } else if (TOOL_VIEWS.includes(view) && result.tree) {
      let inserted = false;
      function addTool(node) {
        const views = leaves(node);
        if (!inserted && views.every(v => v !== 'lua' && TOOL_VIEWS.includes(v))) {
          inserted = true;
          return split(node, leaf(view), 'y', Math.min(.8, views.length / (views.length + 1)));
        }
        if (node.type === 'leaf') return node;
        return { ...node, a: addTool(node.a), b: addTool(node.b) };
      }
      tree = addTool(result.tree);
      if (!inserted) tree = split(tree, leaf(view), axis, axis === 'x' ? .74 : view==='library'?.4:.6);
    } else {
      tree = view === 'edit' && leaves(result.tree).includes('game')
        ? mapLeaf(result.tree, 'game', node => split(node, leaf(view), 'y'))
        : result.tree ? split(result.tree, leaf(view), axis) : leaf(view);
    }
    result = { ...result, tree };
  }
  if (axis === 'y' && previous.visible.length <= 2 && leaves(result.tree).length === 3 && result.tree.a?.view === 'node' && result.tree?.type === 'split') {
    result = { ...result, tree: { ...result.tree, ratio: 1 / 3 } };
  }
  return result;
}
