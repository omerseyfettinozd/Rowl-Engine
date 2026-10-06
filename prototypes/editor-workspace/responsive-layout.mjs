// Presentation only: the saved dock tree keeps its desktop placement and ratios.
const widths = {node:360, game:340, edit:340, lua:360, inspector:320, library:320, console:320};
export const minimumWidth = tree => !tree ? 0 : tree.type === 'leaf'
  ? widths[tree.view] ?? 280
  : tree.axis === 'x' ? minimumWidth(tree.a) + minimumWidth(tree.b) + 8 : Math.max(minimumWidth(tree.a), minimumWidth(tree.b));
export const stackLayout = (tree, width) => width < 650 || width < minimumWidth(tree);
export function fitDockTree(tree, width) {
  if (!tree || tree.type === 'leaf') return tree;
  if (tree.axis === 'y') return {...tree, a:fitDockTree(tree.a,width), b:fitDockTree(tree.b,width)};
  const space = width - 8;
  const ratio = Math.max(minimumWidth(tree.a)/space, Math.min(1-minimumWidth(tree.b)/space, tree.ratio));
  return {...tree, ratio, a:fitDockTree(tree.a,space*ratio), b:fitDockTree(tree.b,space*(1-ratio))};
}
export const minimumStackHeight = view => view === 'node' ? 360 : view === 'lua' ? 320 : 240;
export function preferredStackHeight(view, width, aspect=1.6) {
  if (view === 'node') return 620;
  if (view === 'lua' || view === 'library') return 440;
  if (view === 'inspector') return 560;
  if (view === 'game' || view === 'edit') return Math.max(280, Math.round((width-22)/aspect+104));
  return 340;
}
export function resizeStackPair(first, second, delta, firstMin=240, secondMin=240) {
  const total = first + second;
  const next = Math.max(firstMin, Math.min(total-secondMin, first+delta));
  return [next, total-next];
}
